/* nx_prof.c -- сэмплирующий профайлер главного потока и потока рендера Unity.
 *
 * Зачем: без разгона выигрыш есть только там, где реально тратится время, а
 * скрипты игры -- лишь малая часть загрузки UnityMain: UnityMain ~25% и
 * UnityGfxDeviceWorker ~66% времени ждут в ядре, остальное размазано по
 * движку. Профайлер отвечает на вопросы «кто и чего ждёт» и «сколько стоит
 * фаза целиком».
 *
 * Как: поток профайлера (ядро 1, UnityMain живёт на ядре 0) ~250 раз в секунду
 * на мгновение приостанавливает UnityMain и UnityGfxDeviceWorker, читает
 * регистры и, пока поток стоит, проходит цепочку кадров (AArch64 frame
 * record: [fp] = прежний fp, [fp+8] = адрес возврата) только внутри стека
 * этого потока. Адреса относятся к функциям libunity/libil2cpp по таблице
 * начал функций (tools/make_profile_table.py -> assets/NxPort/nx_prof.bin), к
 * 16-байтным участкам нашего NRO (Mesa, шимы, svc-обёртки -- ожидания) или к
 * «прочему». Раз в 10 с в журнал:
 *   [prof]      доли по модулям и топ собственного времени (PC);
 *   [prof-wait] для выборок в NRO -- первые три кадра движка (кто ждёт);
 *   [prof-incl] включительное время: каждая функция цепочки один раз.
 * Имена подставляет tools/prof_report.py.
 *
 * Пока поток приостановлен, профайлер не берёт никаких блокировок (в том числе
 * журнала): отпускает поток и только потом пишет. Включается profile_sampler. */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nx_prof.h"
#include "diag.h"
#include "util.h"
#include "nx_gameroot.h"

#define PROF_PERIOD_NS  4000000ULL        /* 4 мс -> 250 Гц на поток */
#define PROF_WINDOW_NS  10000000000ULL    /* окно отчёта 10 с */
#define PROF_SELF_SLOTS 4096
#define PROF_INCL_SLOTS 8192
#define PROF_WAIT_SLOTS 1024
#define PROF_DEPTH      32
#define PROF_SCAN_BYTES 4096              /* скан стека на адреса возврата */
#define PROF_TOP_SELF   16
#define PROF_TOP_WAIT   12
#define PROF_TOP_INCL   30

enum { CLS_NONE = 0, CLS_UNITY = 1, CLS_IL2CPP = 2, CLS_NRO = 3, CLS_OTHER = 4 };

typedef struct {
  uintptr_t base, size;
  uint32_t n;
  const uint32_t *starts;   /* начала функций, по возрастанию */
} ProfModule;

typedef struct { uint32_t key, count; } ProfSlot;
typedef struct { uint32_t k[3], count; } ProfWait;

typedef struct {
  const char *name;
  Handle handle;
  uintptr_t stack_lo, stack_hi;
  uint32_t total, cls[5], overflow, failed, waits;
  ProfSlot self[PROF_SELF_SLOTS];
  ProfSlot incl[PROF_INCL_SLOTS];
  ProfWait wait[PROF_WAIT_SLOTS];
} ProfTarget;

static ProfModule g_unity, g_il2cpp;
static uintptr_t g_nro_base, g_nro_size;
static ProfTarget g_targets[2] = { { .name = "UnityMain" }, { .name = "UnityGfxDeviceWorker" } };
static Thread g_prof_thread;
static void *g_table;

static uint32_t func_index(const ProfModule *m, uintptr_t off) {
  uint32_t lo = 0, hi = m->n;          /* последний start <= off */
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (m->starts[mid] <= off) lo = mid + 1; else hi = mid;
  }
  return lo ? lo - 1 : 0;
}

/* адрес -> ключ (класс << 24 | номер функции / участок NRO) */
static uint32_t classify(uintptr_t pc, int *cls_out) {
  int cls;
  uint32_t key;
  if (g_unity.n && pc - g_unity.base < g_unity.size) {
    cls = CLS_UNITY;
    key = ((uint32_t)CLS_UNITY << 24) | func_index(&g_unity, pc - g_unity.base);
  } else if (g_il2cpp.n && pc - g_il2cpp.base < g_il2cpp.size) {
    cls = CLS_IL2CPP;
    key = ((uint32_t)CLS_IL2CPP << 24) | func_index(&g_il2cpp, pc - g_il2cpp.base);
  } else if (pc - g_nro_base < g_nro_size) {
    cls = CLS_NRO;
    key = ((uint32_t)CLS_NRO << 24) | (uint32_t)(((pc - g_nro_base) >> 4) & 0xffffff);
  } else {
    cls = CLS_OTHER;
    key = (uint32_t)CLS_OTHER << 24;
  }
  if (cls_out) *cls_out = cls;
  return key;
}

static void bump(ProfSlot *slots, uint32_t nslots, uint32_t key, uint32_t *overflow) {
  uint32_t h = (key * 2654435761u) & (nslots - 1);
  for (int probe = 0; probe < 64; probe++, h = (h + 1) & (nslots - 1)) {
    ProfSlot *s = &slots[h];
    if (s->key == key && s->count) { s->count++; return; }
    if (!s->count) { s->key = key; s->count = 1; return; }
  }
  (*overflow)++;
}

static void bump_wait(ProfTarget *t, const uint32_t k[3]) {
  uint32_t h = ((k[0] * 2654435761u) ^ (k[1] * 40503u) ^ (k[2] * 2246822519u)) & (PROF_WAIT_SLOTS - 1);
  for (int probe = 0; probe < 64; probe++, h = (h + 1) & (PROF_WAIT_SLOTS - 1)) {
    ProfWait *w = &t->wait[h];
    if (w->count && !memcmp(w->k, k, sizeof w->k)) { w->count++; return; }
    if (!w->count) { memcpy(w->k, k, sizeof w->k); w->count = 1; return; }
  }
  t->overflow++;
}

/* Цепочка адресов (PC, LR, затем адреса возврата по frame record). Вызывается,
 * пока поток приостановлен; читает только внутри его стека. */
static int unwind(ProfTarget *t, const ThreadContext *ctx, uintptr_t *out) {
  int n = 0;
  out[n++] = (uintptr_t)ctx->pc.x;
  if (ctx->lr) out[n++] = (uintptr_t)ctx->lr;
  const uintptr_t sp = (uintptr_t)ctx->sp;
  /* sp вне запомненного стека (или он ещё не известен) -- спросить заново */
  if (!t->stack_lo || sp < t->stack_lo || sp >= t->stack_hi) {
    MemoryInfo mi; u32 pi;
    t->stack_lo = t->stack_hi = 0;
    if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, sp)) && mi.size >= 32 &&
        mi.type != MemType_Unmapped && (mi.perm & Perm_R)) {
      t->stack_lo = mi.addr; t->stack_hi = mi.addr + mi.size;
    }
  }
  if (!t->stack_lo) return n;
  /* Проверка «fp + 16 <= stack_hi» переполняется на мусорном
   * fp (0xfffffffffffffff8 + 16 = 8), и чтение пошло бы по адресу 0. Сравниваем без
   * сложения. */
  const uintptr_t fp_max = t->stack_hi - 16;
  uintptr_t fp = (uintptr_t)ctx->fp;
  while (n < PROF_DEPTH && fp >= t->stack_lo && fp <= fp_max && !(fp & 7)) {
    const uintptr_t next = ((const uintptr_t *)fp)[0];
    const uintptr_t ret = ((const uintptr_t *)fp)[1];
    if (!ret) break;
    if (ret != out[n - 1]) out[n++] = ret;
    if (next <= fp) break;              /* стек растёт вниз: кадры вызывающих выше */
    fp = next;
  }
  /* libunity собрана без указателей кадров, и цепочка выше обрывается на
   * первой же её функции (до 22% ожиданий UnityMain без вызывающего). Как и
   * сторож, сканируем стек на адреса возврата в libunity/libil2cpp. Слово
   * засчитываем, только если перед ним стоит BL/BLR -- значит, это адрес
   * возврата из вызова, а не случайное число. Старые адреса мёртвых кадров
   * изредка попадаются, но порядок от sp вверх сохраняет вложенность. */
  {
    /* sp уже проверен: stack_lo <= sp < stack_hi; конец скана без переполнения */
    const uintptr_t lim = (t->stack_hi - sp > PROF_SCAN_BYTES) ? sp + PROF_SCAN_BYTES : t->stack_hi;
    const uintptr_t *w = (const uintptr_t *)((sp + 7) & ~(uintptr_t)7);
    const uintptr_t *wend = (const uintptr_t *)(lim & ~(uintptr_t)7);
    for (; w < wend && n < PROF_DEPTH; w++) {
      const uintptr_t a = *w;
      const int in_unity = g_unity.n && a - g_unity.base < g_unity.size && a - g_unity.base >= 4;
      const int in_il2cpp = g_il2cpp.n && a - g_il2cpp.base < g_il2cpp.size && a - g_il2cpp.base >= 4;
      if (!(in_unity || in_il2cpp) || (a & 3)) continue;
      const uint32_t insn = *(const uint32_t *)(a - 4);
      if ((insn & 0xFC000000u) != 0x94000000u && (insn & 0xFFFFFC1Fu) != 0xD63F0000u) continue;
      int dup = 0;
      for (int j = 0; j < n; j++) if (out[j] == a) { dup = 1; break; }
      if (!dup) out[n++] = a;
    }
  }
  return n;
}

static void account(ProfTarget *t, const uintptr_t *chain, int n) {
  int cls;
  const uint32_t self = classify(chain[0], &cls);
  t->total++;
  t->cls[cls]++;
  bump(t->self, PROF_SELF_SLOTS, self, &t->overflow);
  /* включительно: каждая функция цепочки один раз */
  uint32_t seen[PROF_DEPTH];
  int nseen = 0;
  uint32_t wk[3] = { 0, 0, 0 };
  int nw = 0;
  for (int i = 0; i < n; i++) {
    int c;
    const uint32_t key = classify(chain[i], &c);
    if (c == CLS_OTHER) continue;
    int dup = 0;
    for (int j = 0; j < nseen; j++) if (seen[j] == key) { dup = 1; break; }
    if (!dup) {
      seen[nseen++] = key;
      bump(t->incl, PROF_INCL_SLOTS, key, &t->overflow);
    }
    if (cls == CLS_NRO && nw < 3 && (c == CLS_UNITY || c == CLS_IL2CPP)) wk[nw++] = key;
  }
  if (cls == CLS_NRO) {
    t->waits++;
    if (nw) bump_wait(t, wk);
  }
}

static int cmp_slot(const void *a, const void *b) {
  const ProfSlot *x = a, *y = b;
  return (int)y->count - (int)x->count;
}
static int cmp_wait(const void *a, const void *b) {
  const ProfWait *x = a, *y = b;
  return (int)y->count - (int)x->count;
}

static void fmt_key(char *buf, size_t n, uint32_t key) {
  const uint32_t cls = key >> 24, idx = key & 0xffffff;
  if (cls == CLS_UNITY && idx < g_unity.n)        snprintf(buf, n, "libunity.so+0x%x", g_unity.starts[idx]);
  else if (cls == CLS_IL2CPP && idx < g_il2cpp.n) snprintf(buf, n, "libil2cpp.so+0x%x", g_il2cpp.starts[idx]);
  else if (cls == CLS_NRO)                         snprintf(buf, n, "NRO+0x%x", idx << 4);
  else                                             snprintf(buf, n, "other");
}

static void report(ProfTarget *t) {
  if (!t->total) return;
  const float k = 100.0f / (float)t->total;
  char a[48], b[48], c[48];
  debugPrintf("[prof] %s n=%u unity=%.1f%% il2cpp=%.1f%% nro=%.1f%% other=%.1f%% waits=%.1f%% overflow=%u failed=%u\n",
              t->name, t->total, t->cls[CLS_UNITY] * k, t->cls[CLS_IL2CPP] * k, t->cls[CLS_NRO] * k,
              t->cls[CLS_OTHER] * k, t->waits * k, t->overflow, t->failed);
  qsort(t->self, PROF_SELF_SLOTS, sizeof(ProfSlot), cmp_slot);
  for (int i = 0; i < PROF_TOP_SELF && t->self[i].count; i++) {
    fmt_key(a, sizeof a, t->self[i].key);
    debugPrintf("[prof] %s %5.1f%% %s\n", t->name, t->self[i].count * k, a);
  }
  qsort(t->wait, PROF_WAIT_SLOTS, sizeof(ProfWait), cmp_wait);
  for (int i = 0; i < PROF_TOP_WAIT && t->wait[i].count; i++) {
    fmt_key(a, sizeof a, t->wait[i].k[0]);
    if (t->wait[i].k[1]) fmt_key(b, sizeof b, t->wait[i].k[1]); else b[0] = 0;
    if (t->wait[i].k[2]) fmt_key(c, sizeof c, t->wait[i].k[2]); else c[0] = 0;
    debugPrintf("[prof-wait] %s %5.1f%% %s < %s < %s\n", t->name, t->wait[i].count * k, a, b, c);
  }
  qsort(t->incl, PROF_INCL_SLOTS, sizeof(ProfSlot), cmp_slot);
  for (int i = 0; i < PROF_TOP_INCL && t->incl[i].count; i++) {
    fmt_key(a, sizeof a, t->incl[i].key);
    debugPrintf("[prof-incl] %s %5.1f%% %s\n", t->name, t->incl[i].count * k, a);
  }
  memset(t->self, 0, sizeof t->self);
  memset(t->incl, 0, sizeof t->incl);
  memset(t->wait, 0, sizeof t->wait);
  memset(t->cls, 0, sizeof t->cls);
  t->total = t->overflow = t->failed = t->waits = 0;
}

static void prof_main(void *arg) {
  (void)arg;
  uint64_t window_start = armTicksToNs(armGetSystemTick());
  uintptr_t chain[PROF_DEPTH];
  for (;;) {
    svcSleepThread(PROF_PERIOD_NS);
    for (int i = 0; i < 2; i++) {
      ProfTarget *t = &g_targets[i];
      if (!t->handle) {
        t->handle = diag_find_name_handle(t->name);   /* блокировка реестра -- до любой паузы */
        if (!t->handle) continue;
      }
      if (R_FAILED(svcSetThreadActivity(t->handle, ThreadActivity_Paused))) { t->failed++; continue; }
      ThreadContext ctx;
      const Result r = svcGetThreadContext3(&ctx, t->handle);
      const int n = R_SUCCEEDED(r) ? unwind(t, &ctx, chain) : 0;   /* стек читаем, пока поток стоит */
      svcSetThreadActivity(t->handle, ThreadActivity_Runnable);
      if (!n) { t->failed++; continue; }
      account(t, chain, n);
    }
    const uint64_t now = armTicksToNs(armGetSystemTick());
    if (now - window_start >= PROF_WINDOW_NS) {
      for (int i = 0; i < 2; i++) report(&g_targets[i]);
      window_start = now;
    }
  }
}

static int load_table(void) {
  char path[320];
  game_path(path, sizeof path, "assets/NxPort/nx_prof.bin");
  FILE *f = fopen(path, "rb");
  if (!f) { debugPrintf("[prof] нет %s -- профайлер выключен\n", path); return 0; }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  g_table = size > 12 ? malloc((size_t)size) : NULL;
  if (!g_table || fread(g_table, 1, (size_t)size, f) != (size_t)size) {
    fclose(f);
    debugPrintf("[prof] не прочитать %s\n", path);
    return 0;
  }
  fclose(f);
  const uint8_t *p = g_table, *end = p + size;
  uint32_t version, nmods;
  if (memcmp(p, "NXPF", 4)) { debugPrintf("[prof] %s: не таблица NXPF\n", path); return 0; }
  memcpy(&version, p + 4, 4);
  memcpy(&nmods, p + 8, 4);
  p += 12;
  for (uint32_t m = 0; m < nmods && p + 36 <= end; m++) {
    char name[33] = { 0 };
    uint32_t n;
    memcpy(name, p, 32);
    memcpy(&n, p + 32, 4);
    p += 36;
    if (p + (size_t)n * 4 > end) break;
    ProfModule *dst = !strcmp(name, "libunity.so") ? &g_unity : !strcmp(name, "libil2cpp.so") ? &g_il2cpp : NULL;
    if (dst) { dst->n = n; dst->starts = (const uint32_t *)p; }
    p += (size_t)n * 4;
  }
  debugPrintf("[prof] таблица v%u: libunity %u функций, libil2cpp %u функций\n", version, g_unity.n, g_il2cpp.n);
  return g_unity.n || g_il2cpp.n;
}

void nx_prof_start(uintptr_t unity_base, size_t unity_size, uintptr_t il2cpp_base, size_t il2cpp_size) {
  g_unity.base = unity_base;   g_unity.size = unity_size;
  g_il2cpp.base = il2cpp_base; g_il2cpp.size = il2cpp_size;
  MemoryInfo mi; u32 pi;
  if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, (u64)(uintptr_t)&nx_prof_start))) {
    g_nro_base = mi.addr; g_nro_size = mi.size;
  }
  if (!load_table()) return;
  /* ядро 1: UnityMain живёт на ядре 0; приоритет чуть выше главного, чтобы
   * выборки шли ровно и не копились */
  if (R_SUCCEEDED(threadCreate(&g_prof_thread, prof_main, NULL, NULL, 0x4000, 0x2B, 1)) &&
      R_SUCCEEDED(threadStart(&g_prof_thread)))
    debugPrintf("[prof] запущен v3: 250 Гц, окно 10 с, стек до %d кадров + скан %d байт стека, "
                "UnityMain и UnityGfxDeviceWorker\n", PROF_DEPTH, PROF_SCAN_BYTES);
  else
    debugPrintf("[prof] поток профайлера не запустился\n");
}
