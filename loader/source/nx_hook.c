/* nx_hook.c -- замер функций движка по входу: что внутри FinishFrameRendering.
 *
 * В бою PostLateUpdate.FinishFrameRendering (PlayerRender) занимает 10-13 мс
 * главного потока, из них ожидание 1-1.5 -- остальное своя работа:
 * отсечение, цикл рендера, постобработка, интерфейс. Фазы player loop внутрь
 * не заглядывают, поэтому функции из nx_frame_hooks (nx_frame_table.h)
 * перехватываются на входе:
 *   - вход функции (16 байт) -> ldr x16,#8; br x16; адрес переходника слота;
 *   - переходник сохраняет аргументы (x0-x8, q0-q7), берёт время, зовёт
 *     оригинал через «батут» и после возврата учитывает время;
 *   - батут -- снятые 4 инструкции пролога и прыжок на вход+16, лежит в
 *     отдельной странице кода (svcMapProcessCodeMemory, как .so в so_util.c).
 * Пролог сверяется с таблицей и не должен зависеть от PC (это проверяет и
 * генератор). Аргументы только в регистрах: переходник сдвигает стек.
 * Считается только главный поток; ожидания (futex, сон) приписываются самой
 * вложенной замеряемой функции. */
#include <switch.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "nx_frame.h"
#include "nx_frame_table.h"
#include "nx_warp.h"
#include "so_util.h"
#include "util.h"

#define NXH_SLOTS 64
#define NXH_DEPTH 16

uintptr_t nxh_tramp[NXH_SLOTS];             /* читает переходник (asm) */
static const char *nxh_name[NXH_SLOTS];
static uint64_t nxh_ticks[NXH_SLOTS], nxh_calls[NXH_SLOTS], nxh_wait[NXH_SLOTS];
static unsigned nxh_used, nxh_depth;
static struct { unsigned slot; int cam; } nxh_stack[NXH_DEPTH];

/* Учёт по камерам. Camera::CustomRender вызывается 3-4 раза за кадр, это почти
 * весь FinishFrameRendering в бою -- по каждой камере своё
 * время, отсечение (CullScene) и цикл рендера (DoRenderLoop), имя -- через
 * Unity::Component::GetName() во время её же рендера, пока объект жив. */
#define NXH_CAMS 8
static struct { uintptr_t obj; char name[40]; int role; uint64_t ticks, calls, cull, loop; uint64_t inner[NXH_SLOTS]; } nxh_cam[NXH_CAMS];
static int nxh_slot_cam = -1, nxh_slot_cull = -1, nxh_slot_loop = -1, nxh_slot_overlays = -1;
static const char *(*nxh_get_name)(uintptr_t);

static int cam_index(uintptr_t obj) {
  for (int i = 0; i < NXH_CAMS; i++) if (nxh_cam[i].obj == obj) return i;
  for (int i = 0; i < NXH_CAMS; i++) if (!nxh_cam[i].obj) {
    nxh_cam[i].obj = obj;
    const char *name = nxh_get_name ? nxh_get_name(obj) : NULL;
    snprintf(nxh_cam[i].name, sizeof nxh_cam[i].name, "%s", name ? name : "?");
    nxh_cam[i].role = nx_warp_role(nxh_cam[i].name);   /* снимки для перепроецирования */
    return i;
  }
  return -1;
}
static int current_cam(void) {
  for (unsigned d = nxh_depth < NXH_DEPTH ? nxh_depth : NXH_DEPTH; d--; )
    if (nxh_stack[d].cam >= 0) return nxh_stack[d].cam;
  return -1;
}

uint64_t nx_hook_enter(unsigned slot, uintptr_t self);
void nx_hook_exit(unsigned slot, uint64_t start);
void nxh_thunk_0(void);

/* Переходники: nxh_thunk_N = mov x17,#N; b nxh_common -- по 8 байт подряд. */
__asm__(
  ".text\n"
  ".balign 8\n"
  ".global nxh_thunk_0\n"
  ".macro NXH_THUNK n\n"
  "  mov x17, #\\n\n"
  "  b nxh_common\n"
  ".endm\n"
  "nxh_thunk_0:\n"
  ".irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,"
          "32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63\n"
  "  NXH_THUNK \\n\n"
  ".endr\n"
  "nxh_common:\n"
  "  stp x29, x30, [sp, #-240]!\n"
  "  mov x29, sp\n"
  "  stp x0, x1, [sp, #16]\n"
  "  stp x2, x3, [sp, #32]\n"
  "  stp x4, x5, [sp, #48]\n"
  "  stp x6, x7, [sp, #64]\n"
  "  stp x8, x17, [sp, #80]\n"
  "  stp q0, q1, [sp, #96]\n"
  "  stp q2, q3, [sp, #128]\n"
  "  stp q4, q5, [sp, #160]\n"
  "  stp q6, q7, [sp, #192]\n"
  "  mov x1, x0\n"                             /* первый аргумент (this) -- в учёт по камерам */
  "  mov w0, w17\n"
  "  bl nx_hook_enter\n"
  "  str x0, [sp, #224]\n"
  "  ldp q6, q7, [sp, #192]\n"
  "  ldp q4, q5, [sp, #160]\n"
  "  ldp q2, q3, [sp, #128]\n"
  "  ldp q0, q1, [sp, #96]\n"
  "  ldr x17, [sp, #88]\n"
  "  adrp x16, nxh_tramp\n"
  "  add x16, x16, #:lo12:nxh_tramp\n"
  "  ldr x16, [x16, x17, lsl #3]\n"
  "  ldp x8, x17, [sp, #80]\n"
  "  ldp x6, x7, [sp, #64]\n"
  "  ldp x4, x5, [sp, #48]\n"
  "  ldp x2, x3, [sp, #32]\n"
  "  ldp x0, x1, [sp, #16]\n"
  "  blr x16\n"
  "  stp x0, x1, [sp, #16]\n"
  "  stp q0, q1, [sp, #96]\n"
  "  stp q2, q3, [sp, #128]\n"
  "  ldr x0, [sp, #88]\n"
  "  ldr x1, [sp, #224]\n"
  "  bl nx_hook_exit\n"
  "  ldp q0, q1, [sp, #96]\n"
  "  ldp q2, q3, [sp, #128]\n"
  "  ldp x0, x1, [sp, #16]\n"
  "  ldp x29, x30, [sp], #240\n"
  "  ret\n"
);

uint64_t nx_hook_enter(unsigned slot, uintptr_t self) {
  if (!nx_frame_on_main()) return 0;
  if (nxh_depth < NXH_DEPTH) {
    nxh_stack[nxh_depth].slot = slot;
    const int cam = (int)slot == nxh_slot_cam && self ? cam_index(self) : -1;
    nxh_stack[nxh_depth].cam = cam;
    if (cam >= 0 && nxh_cam[cam].role) nx_warp_camera(nxh_cam[cam].role, self, 0);
  }
  if ((int)slot == nxh_slot_overlays) nx_warp_overlays();
  nxh_depth++;
  return armGetSystemTick();
}
void nx_hook_exit(unsigned slot, uint64_t start) {
  if (!start) return;
  const uint64_t dt = armGetSystemTick() - start;
  nxh_ticks[slot] += dt;
  nxh_calls[slot]++;
  if (nxh_depth) nxh_depth--;
  if (nxh_depth >= NXH_DEPTH) return;
  const int own = nxh_stack[nxh_depth].cam;   /* сама камера (CustomRender) */
  if (own >= 0 && nxh_cam[own].role) nx_warp_camera(nxh_cam[own].role, nxh_cam[own].obj, 1);
  if (own >= 0) { nxh_cam[own].ticks += dt; nxh_cam[own].calls++; return; }
  const int cam = current_cam();               /* отсечение/цикл внутри камеры */
  if (cam < 0) return;
  if ((int)slot == nxh_slot_cull) nxh_cam[cam].cull += dt;
  else if ((int)slot == nxh_slot_loop) nxh_cam[cam].loop += dt;
  /* всё, что замеряется внутри камеры, -- по функциям (вложенные вызовы
   * одной функции считаются дважды только при рекурсии, её здесь нет) */
  if (nxh_depth && nxh_stack[nxh_depth - 1].cam == cam) nxh_cam[cam].inner[slot] += dt;
}
void nx_hook_main_wait(uint64_t ticks) {
  if (nxh_depth && nxh_depth <= NXH_DEPTH) nxh_wait[nxh_stack[nxh_depth - 1].slot] += ticks;
}

void nx_hook_install(uintptr_t ub, size_t usize) {
  /* Detailed detours include ScriptingInvocation::Invoke, called hundreds
   * of times per frame. Lightweight phase stats do not need these hooks.
   * Keep the experimental reprojection capture path when explicitly enabled. */
  if (!(config.engine_profiler && config.frame_stats) && !config.reproject) return;
  const unsigned total = sizeof nx_frame_hooks / sizeof nx_frame_hooks[0];
  /* батуты: 8 слов на слот -- 4 инструкции пролога, ldr x16,#8; br x16; адрес */
  uint32_t *code = memalign(0x1000, 0x1000);
  if (!code) return;
  memset(code, 0, 0x1000);
  uintptr_t fns[NXH_SLOTS];
  unsigned n = 0;
  for (unsigned i = 0; i < total && n < NXH_SLOTS; i++) {
    const uintptr_t fn = ub + nx_frame_hooks[i].off;
    if (nx_frame_hooks[i].off >= usize || memcmp((const void *)fn, nx_frame_hooks[i].prologue, 16)) {
      debugPrintf("[hook] %s: пролог не совпал -- без замера\n", nx_frame_hooks[i].name);
      continue;
    }
    uint32_t *t = code + n * 8;
    memcpy(t, nx_frame_hooks[i].prologue, 16);
    t[4] = 0x58000050u;   /* ldr x16, #8 */
    t[5] = 0xd61f0200u;   /* br  x16     */
    memcpy(t + 6, &(uint64_t){ fn + 16 }, 8);
    fns[n] = fn;
    nxh_name[n] = nx_frame_hooks[i].name;
    if (!strcmp(nxh_name[n], "Camera::CustomRender")) nxh_slot_cam = (int)n;
    else if (!strcmp(nxh_name[n], "CullScene")) nxh_slot_cull = (int)n;
    else if (!strcmp(nxh_name[n], "DoRenderLoop")) nxh_slot_loop = (int)n;
    else if (!strcmp(nxh_name[n], "UI::RenderOverlays")) nxh_slot_overlays = (int)n;
    n++;
  }
  const uint32_t *gn = (const uint32_t *)(ub + NX_COMPONENT_GET_NAME);
  if (NX_COMPONENT_GET_NAME < usize && gn[0] == NX_COMPONENT_GET_NAME_W0 && gn[1] == NX_COMPONENT_GET_NAME_W1)
    nxh_get_name = (const char *(*)(uintptr_t))gn;
  if (!n) { free(code); return; }
  armDCacheFlush(code, 0x1000);
  virtmemLock();
  void *exec = virtmemFindCodeMemory(0x1000, 0x1000);
  VirtmemReservation *rv = exec ? virtmemAddReservation(exec, 0x1000) : NULL;
  virtmemUnlock();
  Result rc = exec ? svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)exec, (u64)code, 0x1000) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
  if (R_SUCCEEDED(rc)) rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)exec, 0x1000, Perm_Rx);
  if (R_FAILED(rc)) {
    debugPrintf("[hook] страница батутов не создана rc=0x%x -- замер функций выключен\n", rc);
    if (rv) { virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock(); }
    return;   /* code остаётся занятым: после неудачного map его состояние не гарантировано */
  }
  for (unsigned s = 0; s < n; s++) {
    nxh_tramp[s] = (uintptr_t)exec + s * 32;
    const uintptr_t thunk = (uintptr_t)nxh_thunk_0 + s * 8;
    const uint32_t stub[4] = { 0x58000050u, 0xd61f0200u, (uint32_t)thunk, (uint32_t)(thunk >> 32) };
    const int prc = so_patch_code((void *)fns[s], stub, sizeof stub);
    if (prc) { debugPrintf("[hook] %s: заплатка входа rc=%d\n", nxh_name[s], prc); nxh_name[s] = NULL; }
  }
  nxh_used = n;
  debugPrintf("[hook] замер функций отрисовки: %u из %u, батуты @%p\n", n, total, exec);
}

void nx_hook_report(double frames) {
  for (unsigned s = 0; s < nxh_used; s++) {
    if (nxh_name[s] && nxh_calls[s])
      debugPrintf("[frame-r] %6.2f мс x%4.2f ожид %5.2f  %s\n", (double)armTicksToNs(nxh_ticks[s]) / 1e6 / frames,
                  (double)nxh_calls[s] / frames, (double)armTicksToNs(nxh_wait[s]) / 1e6 / frames, nxh_name[s]);
    nxh_ticks[s] = nxh_calls[s] = nxh_wait[s] = 0;
  }
  for (int i = 0; i < NXH_CAMS; i++) {
    if (nxh_cam[i].obj && nxh_cam[i].calls)
      debugPrintf("[frame-cam] %6.2f мс x%4.2f отсечение %5.2f цикл %5.2f  %s\n",
                  (double)armTicksToNs(nxh_cam[i].ticks) / 1e6 / frames, (double)nxh_cam[i].calls / frames,
                  (double)armTicksToNs(nxh_cam[i].cull) / 1e6 / frames,
                  (double)armTicksToNs(nxh_cam[i].loop) / 1e6 / frames, nxh_cam[i].name);
    if (nxh_cam[i].obj && nxh_cam[i].calls) {
      /* прямые вызовы внутри CustomRender этой камеры и остаток без замера */
      char line[900]; size_t n = 0; uint64_t covered = 0;
      for (unsigned s = 0; s < nxh_used; s++) {
        covered += nxh_cam[i].inner[s];
        const double t = (double)armTicksToNs(nxh_cam[i].inner[s]) / 1e6 / frames;
        if (t >= 0.03 && n < sizeof line)
          n += snprintf(line + n, sizeof line - n, "%s%s %.2f", n ? " | " : "", nxh_name[s], t);
      }
      const double rest = (double)armTicksToNs(nxh_cam[i].ticks - (covered < nxh_cam[i].ticks ? covered : nxh_cam[i].ticks)) / 1e6 / frames;
      debugPrintf("[frame-cami] %s: %s | без замера %.2f\n", nxh_cam[i].name, n ? line : "-", rest);
    }
    /* таблица заново каждое окно: после смены сцены адрес камеры может занять другая */
    memset(&nxh_cam[i], 0, sizeof nxh_cam[i]);
  }
}
