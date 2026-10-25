/* nx_game_fixes.c -- заплатки ошибок кода игры (libil2cpp) на входе функций.
 *
 * Пятна крови (BloodsplatterManager / BloodstainParent). Порт режет их число
 * (bloodStainMax 512, LocalPrefs nxVisualBudgetV1), поэтому в долгом бою пятна
 * начинают переиспользоваться, и в этой ветке у игры две ошибки (уровень 1-2,
 * ~2.5 мин боя):
 *
 * 1. RemoveStain(index) берёт позицию пятна в списке родителя из
 *    bsm.indexInParentList[index], но слот родителя к этому времени мог отойти
 *    другой поверхности -- позиция за концом её списка, и List.set_Item бросает
 *    ArgumentOutOfRangeException (812 исключений за сеанс, каждое с трассой).
 *    Заплатка: если children[позиция] != index, связь пятна просто снимается
 *    (parentOfStain[index] = -1), чужой список не трогается.
 *
 * 2. ClearChildren() при выгрузке уровня: BloodsplatterManager.OnDestroy уже
 *    освободил NativeArray props (m_Buffer = NULL), а сам объект ещё жив, и
 *    проверка `bsm == null` его пропускает -- запись props[i] = default по
 *    адресу 0 + i*32, Data Abort 0x3e20 (вылет при переходе 1-2 -> 1-3).
 *    Заплатка: при props.m_Buffer == NULL выход.
 *
 * Обе функции перехватываются на входе: первые 16 байт -> ldr x16,#8; br x16;
 * адрес обёртки. Снятые инструкции повторяет переходник ниже и прыгает дальше
 * в оригинал. Перед заплаткой сверяются пролог и все смещения полей, на
 * которые опираются обёртки: другая сборка libil2cpp просто останется без них. */
#include <switch.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "nx_frame.h"
#include "nx_offsets.h"
#include "so_util.h"
#include "util.h"

/* смещения полей (сверяются по коду) */
#define BSP_CHILDREN   104   /* BloodstainParent.children: List<int> */
#define BSP_BSM        120   /* BloodstainParent.bsm */
#define BSM_PROPS      280   /* BloodsplatterManager.props.m_Buffer */
#define BSM_PARENT_OF  360   /* BloodsplatterManager.parentOfStain: int[] */
#define BSM_INDEX_IN   368   /* BloodsplatterManager.indexInParentList: int[] */
#define LIST_ITEMS     16
#define LIST_SIZE      24
#define ARRAY_LENGTH   24
#define ARRAY_DATA     32

uintptr_t nx_bs_clear_x21, nx_bs_clear_ret, nx_bs_remove_ret, nx_aim_update_ret;
void nx_bs_clear_tramp(void *self, const void *method);
void nx_bs_remove_tramp(void *self, int32_t index, const void *method);
void nx_aim_update_tramp(void *self, const void *method);
__asm__(
  ".text\n"
  ".balign 4\n"
  ".global nx_bs_clear_tramp\n"
  ".type nx_bs_clear_tramp, %function\n"
  "nx_bs_clear_tramp:\n"                       /* пролог ClearChildren */
  "  str x30, [sp, #-48]!\n"
  "  stp x22, x21, [sp, #16]\n"
  "  stp x20, x19, [sp, #32]\n"
  "  adrp x16, nx_bs_clear_x21\n"              /* adrp x21 оригинала -- готовым значением */
  "  ldr x21, [x16, #:lo12:nx_bs_clear_x21]\n"
  "  adrp x16, nx_bs_clear_ret\n"
  "  ldr x16, [x16, #:lo12:nx_bs_clear_ret]\n"
  "  br x16\n"
  ".size nx_bs_clear_tramp, .-nx_bs_clear_tramp\n"
  ".global nx_bs_remove_tramp\n"
  ".type nx_bs_remove_tramp, %function\n"
  "nx_bs_remove_tramp:\n"                      /* пролог RemoveStain */
  "  str x30, [sp, #-64]!\n"
  "  stp x24, x23, [sp, #16]\n"
  "  stp x22, x21, [sp, #32]\n"
  "  stp x20, x19, [sp, #48]\n"
  "  adrp x16, nx_bs_remove_ret\n"
  "  ldr x16, [x16, #:lo12:nx_bs_remove_ret]\n"
  "  br x16\n"
  ".size nx_bs_remove_tramp, .-nx_bs_remove_tramp\n"
  ".global nx_aim_update_tramp\n"
  ".type nx_aim_update_tramp, %function\n"
  "nx_aim_update_tramp:\n"                     /* пролог CameraFrustumTargeter.Update */
  "  stp d15, d14, [sp, #-160]!\n"
  "  stp d13, d12, [sp, #16]\n"
  "  stp d11, d10, [sp, #32]\n"
  "  stp d9, d8, [sp, #48]\n"
  "  adrp x16, nx_aim_update_ret\n"
  "  ldr x16, [x16, #:lo12:nx_aim_update_ret]\n"
  "  br x16\n"
  ".size nx_aim_update_tramp, .-nx_aim_update_tramp\n"
);

static unsigned g_clear_skipped, g_remove_skipped;

static inline int32_t *int_array(char *array, int32_t index) {
  if (!array || index < 0 || (uint64_t)index >= *(uint64_t *)(array + ARRAY_LENGTH)) return NULL;
  return (int32_t *)(array + ARRAY_DATA) + index;
}

static void bs_clear_hook(void *self, const void *method) {
  char *bsm = self ? *(char **)((char *)self + BSP_BSM) : NULL;
  if (bsm && !*(void **)(bsm + BSM_PROPS)) {
    const unsigned n = ++g_clear_skipped;
    if (n <= 3 || n % 100 == 0)
      debugPrintf("[fix] BloodstainParent.ClearChildren: props уже освобождён -- пропуск (%u)\n", n);
    return;
  }
  nx_bs_clear_tramp(self, method);
}

static void bs_remove_hook(void *self, int32_t index, const void *method) {
  char *bsm = self ? *(char **)((char *)self + BSP_BSM) : NULL;
  char *list = self ? *(char **)((char *)self + BSP_CHILDREN) : NULL;
  if (bsm && list) {
    const int32_t *slot = int_array(*(char **)(bsm + BSM_INDEX_IN), index);
    if (slot) {
      const int32_t size = *(int32_t *)(list + LIST_SIZE);
      const int32_t *item = *slot < size ? int_array(*(char **)(list + LIST_ITEMS), *slot) : NULL;
      if (!item || *item != index) {
        int32_t *parent = int_array(*(char **)(bsm + BSM_PARENT_OF), index);
        if (parent) *parent = -1;
        const unsigned n = ++g_remove_skipped;
        if (n <= 3 || n % 100 == 0)
          debugPrintf("[fix] BloodstainParent.RemoveStain(%d): позиция %d вне списка из %d -- связь снята (%u)\n",
                      index, *slot, size, n);
        return;
      }
    }
  }
  nx_bs_remove_tramp(self, index, method);
}

/* Автоприцел (CameraFrustumTargeter.Update) в бою 1.5-2.5 мс на кадр: на
 * каждого кандидата луч с проверкой порталов. autoaim_every N: поиск цели раз
 * в N кадров, в промежутке остаётся найденная (задержка выбора цели -- один
 * кадр). Если текущая цель уже уничтожена (m_CachedPtr == 0), поиск идёт сразу,
 * чтобы оружие и прицел не взяли мёртвый коллайдер. */
#define AIM_TARGET     32    /* CameraFrustumTargeter.CurrentTarget (Collider) */
#define UOBJ_CACHEDPTR 16    /* UnityEngine.Object.m_CachedPtr */
static struct { uintptr_t self; uint64_t frame; } g_aim[4];
static unsigned g_aim_skipped;

/* 1 = искать цель в этом кадре. Зовётся и из пересобранной libil2cpp
 * (NX_AIM_THROTTLE в CameraFrustumTargeter.Update, patch_nx_game_fixes.py)
 * через dlsym("nxb_aim_should_run"), и из перехвата старой сборки ниже. */
int nxb_aim_should_run(void *self, void *target) {
  if (config.autoaim_every <= 1 || !self) return 1;
  if (target && !*(void **)((char *)target + UOBJ_CACHEDPTR)) return 1;   /* цель уничтожена */
  const uint64_t frame = nx_frame_number();
  unsigned i = 0;
  while (i < 4 && g_aim[i].self && g_aim[i].self != (uintptr_t)self) i++;
  if (i >= 4) return 1;
  if (g_aim[i].self && frame - g_aim[i].frame < (uint64_t)config.autoaim_every) {
    g_aim_skipped++;
    return 0;
  }
  g_aim[i].self = (uintptr_t)self;
  g_aim[i].frame = frame;
  return 1;
}

/* Небо Лимбо (LimboSkybox.UpdateCamera рендерит fakeCam вручную): 2.5-2.9 мс на
 * кадр, при 68 FPS это 17% кадра. 1 = рендерить в этом кадре. sky_every: 1 --
 * каждый кадр, 2 -- через кадр, 0 (по умолчанию) -- через кадр, пока FPS ниже 90.
 * Диораму не отделить ни дальностью (её столбы уходят до 150), ни слоем (её
 * освещает свет со слоевой маской).
 * Пропуск оставляет в RenderTexture небо прошлого кадра (отставание на кадр при
 * повороте), положение камеры неба обновляется всё равно. */
static struct { uintptr_t self; uint64_t frame; } g_sky[4];
int nxb_sky_should_render(void *self) {
  int every = config.sky_every;
  if (every == 0) every = nx_frame_interval_ms() > 11.1 ? 2 : 1;
  if (every <= 1 || !self) return 1;
  const uint64_t frame = nx_frame_number();
  unsigned i = 0;
  while (i < 4 && g_sky[i].self && g_sky[i].self != (uintptr_t)self) i++;
  if (i >= 4) return 1;
  if (g_sky[i].self && frame - g_sky[i].frame < (uint64_t)every) return 0;
  g_sky[i].self = (uintptr_t)self;
  g_sky[i].frame = frame;
  return 1;
}

static void aim_update_hook(void *self, const void *method) {
  if (self && !nxb_aim_should_run(self, *(void **)((char *)self + AIM_TARGET))) return;
  nx_aim_update_tramp(self, method);
}

typedef struct { uint32_t at, word; } CodeCheck;

static int code_matches(uintptr_t fn, const CodeCheck *checks, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    if (*(const uint32_t *)(fn + checks[i].at) != checks[i].word) return 0;
  return 1;
}

static int hook_entry(uintptr_t fn, void *hook) {
  const uint32_t stub[4] = {
    0x58000050u,  /* ldr x16, #8 */
    0xd61f0200u,  /* br  x16     */
    (uint32_t)((uintptr_t)hook & 0xffffffffu),
    (uint32_t)((uintptr_t)hook >> 32),
  };
  return so_patch_code((void *)fn, stub, sizeof stub);
}

void nx_game_fixes_install(uintptr_t il2cpp_base) {
  const uint32_t clear = nxo_get("il2cpp.bloodstain_clear");
  if (clear) {
    const uintptr_t fn = il2cpp_base + clear;
    static const CodeCheck checks[] = {
      { 0x00, 0xf81d0ffe }, { 0x04, 0xa90157f6 }, { 0x08, 0xa9024ff4 },  /* пролог */
      { 0x60, 0xf9403e74 },   /* ldr x20, [x19, #120] -- bsm */
      { 0x84, 0xf9403660 },   /* ldr x0,  [x19, #104] -- children */
      { 0x100, 0xf9408d08 },  /* ldr x8,  [x8, #280]  -- props.m_Buffer */
    };
    const uint32_t adrp = *(const uint32_t *)(fn + 12);
    if (code_matches(fn, checks, sizeof checks / sizeof checks[0]) && (adrp & 0x9f00001fu) == 0x90000015u) {
      int64_t imm = (int64_t)((((adrp >> 5) & 0x7ffff) << 2) | ((adrp >> 29) & 3));
      if (imm & (1 << 20)) imm -= 1 << 21;
      nx_bs_clear_x21 = ((fn + 12) & ~(uintptr_t)0xfff) + (uintptr_t)(imm << 12);
      nx_bs_clear_ret = fn + 16;
      debugPrintf("[fix] BloodstainParent.ClearChildren @il2cpp+0x%x: заплатка rc=%d\n", clear,
                  hook_entry(fn, (void *)bs_clear_hook));
    } else debugPrintf("[fix] BloodstainParent.ClearChildren: код не совпал -- без заплатки\n");
  }
  const uint32_t remove = nxo_get("il2cpp.bloodstain_remove");
  if (remove) {
    const uintptr_t fn = il2cpp_base + remove;
    static const CodeCheck checks[] = {
      { 0x00, 0xf81c0ffe }, { 0x04, 0xa9015ff8 }, { 0x08, 0xa90257f6 }, { 0x0c, 0xa9034ff4 },
      { 0x5c, 0xf9403e88 },   /* ldr x8, [x20, #120] -- bsm */
      { 0x64, 0xf940b908 },   /* ldr x8, [x8, #368]  -- indexInParentList */
      { 0x78, 0xf9403680 },   /* ldr x0, [x20, #104] -- children */
      { 0x118, 0xf940b508 },  /* ldr x8, [x8, #360]  -- parentOfStain */
    };
    if (code_matches(fn, checks, sizeof checks / sizeof checks[0])) {
      nx_bs_remove_ret = fn + 16;
      debugPrintf("[fix] BloodstainParent.RemoveStain @il2cpp+0x%x: заплатка rc=%d\n", remove,
                  hook_entry(fn, (void *)bs_remove_hook));
    } else debugPrintf("[fix] BloodstainParent.RemoveStain: код не совпал -- без заплатки\n");
  }
  const uint32_t aim = nxo_get("il2cpp.aim_update");
  if (aim && config.autoaim_every > 1) {
    const uintptr_t fn = il2cpp_base + aim;
    static const CodeCheck checks[] = {
      { 0x00, 0x6db63bef }, { 0x04, 0x6d0133ed }, { 0x08, 0x6d022beb }, { 0x0c, 0x6d0323e9 },
      { 0x134, 0xbd404660 },  /* ldr s0, [x19, #68]  -- maxHorAim */
      { 0x140, 0xf900127f },  /* str xzr, [x19, #32] -- CurrentTarget = null */
      { 0x144, 0x3900a27f },  /* strb wzr, [x19, #40] -- IsAutoAimed = false */
    };
    if (code_matches(fn, checks, sizeof checks / sizeof checks[0])) {
      nx_aim_update_ret = fn + 16;
      debugPrintf("[fix] CameraFrustumTargeter.Update @il2cpp+0x%x: поиск цели раз в %d кадра, заплатка rc=%d\n",
                  aim, config.autoaim_every, hook_entry(fn, (void *)aim_update_hook));
    } else debugPrintf("[fix] CameraFrustumTargeter.Update: код не совпал -- без заплатки\n");
  }
}
