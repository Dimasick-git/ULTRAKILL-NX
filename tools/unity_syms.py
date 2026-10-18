#!/usr/bin/env python3
"""unity_syms.py -- адреса внутри НАШЕЙ libunity.so по её символам.

Загрузчик из порта Papers, Please держал адреса libunity числами, снятыми с
другой сборки движка (JNI_OnLoad 0x49166c против нашего 0x9fb1dc). У нас есть
libunity.sym.so из редактора (96k символов, тот же build-id), поэтому всё
находится по именам и шаблонам инструкций.

    python unity_syms.py --lib libunity.so --sym libunity.sym.so [--scan-regions] [-o unity.ini]
"""

import argparse
import array
import collections
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import derive_offsets as d  # noqa: E402

JNI = {
    "unity.JNI_OnLoad": "JNI_OnLoad",
    "unity.initJni": "_Z7initJniP7_JNIEnvP8_jobjectS2_",
    "unity.nativeRender": "_Z12nativeRenderP7_JNIEnvP8_jobject",
    "unity.nativeInjectEvent": "_Z17nativeInjectEventP7_JNIEnvP8_jobjectS2_i",
    "unity.nativeRecreateGfxState": "_Z22nativeRecreateGfxStateP7_JNIEnvP8_jobjectiS2_",
    "unity.nativeSendSurfaceChangedEvent": "_Z29nativeSendSurfaceChangedEventP7_JNIEnv",
    "unity.nativeResume": "_Z12nativeResumeP7_JNIEnvP8_jobject",
    "unity.nativePause": "_Z11nativePauseP7_JNIEnvP8_jobject",
    "unity.nativeFocusChanged": "_Z18nativeFocusChangedP7_JNIEnvP8_jobjectb",
    "unity.nativeDone": "_Z10nativeDoneP7_JNIEnvP8_jobject",
    "unity.nativeApplicationUnload": "_Z23nativeApplicationUnloadP7_JNIEnvP8_jobject",
    "unity.nativeLowMemory": "_Z15nativeLowMemoryP7_JNIEnvP8_jobject",
    "unity.swappy_force_disable": "_ZN6SwappyL14s_ForceDisableE",
    "unity.fmod_system_setoutput": "_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE",
    "unity.get_time_since_startup": "_Z19GetTimeSinceStartupv",
    "unity.shader_set_max_chunks": "_ZN6Shader27SetMaxChunksRuntimeOverrideEi",
    "unity.shader_get_max_chunks": "_ZN6Shader27GetMaxChunksRuntimeOverrideEv",
    "unity.quality_mip_limit": "_Z55QualitySettings_Set_Custom_PropGlobalTextureMipmapLimiti",
    "unity.quality_shadows": "_Z38QualitySettings_Set_Custom_PropShadows28QualitySettingsShadowQuality",
    "unity.quality_aa": "_Z43QualitySettings_Set_Custom_PropAntiAliasingi",
    "unity.app_target_fps": "_Z42Application_Set_Custom_PropTargetFrameRatei",
    "unity.app_stacktrace": "_Z39Application_CUSTOM_SetStackTraceLogType7LogType17StackTraceLogType",
    "unity.screen_resolution": "_Z36Screen_CUSTOM_SetResolution_Injectedii14FullscreenModeRK11RefreshRate",
    "unity.umbra_load": "_ZN11UmbraModule13LoadUmbraTomeEPKhm",
    "unity.umbra_init": "_ZN5Umbra10TomeLoader14loadFromBufferEPKhmPNS_9AllocatorE",
    "unity.umbra_status": "_ZNK5Umbra4Tome9getStatusEv",
}

# Шаблоны инструкций, связанных с регионами по 256 МБ (2^28), как в заплатке
# Papers, Please: (маска, значение, описание).
REGION_PATTERNS = [
    (0xFFFFFC00, 0xD35CFC00, "lsr x,x,#28"),
    (0xFFFFFFE0, 0x52A20000, "mov w,#0x10000000"),
    (0xFFFFFFE0, 0x12BE0000, "movn w,#0xf000,lsl16 (0x0fffffff)"),
    (0xFFFFFFE0, 0xF2A20000, "movk x,#0x1000,lsl16"),
    (0xFFE0FC00, 0xCB007000, "sub x,x,x,lsl#28"),
]


def nearest_symbol(funcs, addr):
    """funcs: отсортированный список (адрес, размер, имя)."""
    lo, hi = 0, len(funcs) - 1
    best = None
    while lo <= hi:
        m = (lo + hi) // 2
        if funcs[m][0] <= addr:
            best = funcs[m]
            lo = m + 1
        else:
            hi = m - 1
    if best and addr < best[0] + max(best[1], 4):
        return best
    return None


def scan_regions(lib, syms):
    text = lib.section(".text")
    code = lib.read_va(text["addr"], text["size"])
    words = array.array("I")
    words.frombytes(code[: len(code) // 4 * 4])
    funcs = sorted((v, s, n) for n, (v, s) in syms.items() if s > 0)
    per_func = collections.defaultdict(list)
    for i, w in enumerate(words):
        for mask, val, desc in REGION_PATTERNS:
            if (w & mask) == val:
                addr = text["addr"] + i * 4
                f = nearest_symbol(funcs, addr)
                per_func[f[2] if f else "?"].append((addr, w, desc))
    ranked = sorted(per_func.items(), key=lambda kv: -len(kv[1]))
    return ranked


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", required=True)
    ap.add_argument("--sym", required=True)
    ap.add_argument("--scan-regions", action="store_true")
    ap.add_argument("-o", "--out")
    a = ap.parse_args()
    lib, sym = d.Elf(a.lib), d.Elf(a.sym)
    if lib.build_id() != sym.build_id():
        raise SystemExit("build-id libunity.so и libunity.sym.so не совпадают")
    syms = sym.symbols()
    out = {}
    for key, name in JNI.items():
        if name in syms:
            out[key] = syms[name][0]
        else:
            print(f"  - нет символа {name}")
    for k, v in out.items():
        print(f"{k} = 0x{v:x}")
    if a.out:
        lines, notes = offsets_lines(lib, syms)
        Path(a.out).write_text("# libunity " + (lib.build_id() or "?") + "\n" + "\n".join(lines) + "\n",
                               encoding="utf-8")
        print("\n".join(lines))
        for n in notes:
            print("  -", n)
    if a.scan_regions:
        print("\n== функции с инструкциями регионов 256 МБ")
        for name, hits in scan_regions(lib, syms)[:25]:
            kinds = collections.Counter(h[2] for h in hits)
            print(f"  {len(hits):3d}  {name[:90]}  {dict(kinds)}")




# ---------------------------------------------------------------- заплатки

def _words(lib, addr, size):
    return array.array("I", lib.read_va(addr, size // 4 * 4))


def _b(pc, target):
    return 0x14000000 | (((target - pc) >> 2) & 0x3FFFFFF)


def _bl_target(w, pc):
    if (w & 0xFC000000) != 0x94000000:
        return None
    off = w & 0x3FFFFFF
    if off & 0x2000000:
        off -= 0x4000000
    return pc + off * 4


def _cond_target(w, pc):
    imm = (w >> 5) & 0x7FFFF
    if imm & 0x40000:
        imm -= 0x80000
    return pc + imm * 4


def _adrp_add(lib, a, sz):
    """Пары «adrp xN; ... add xN,xN,#imm» в функции: (адрес add, слово add, цель)."""
    w = _words(lib, a, sz)
    out = []
    for i, x in enumerate(w):
        if (x & 0x9F000000) != 0x90000000:
            continue
        rd = x & 31
        imm = ((x >> 29) & 3) | (((x >> 5) & 0x7FFFF) << 2)
        if imm & (1 << 20):
            imm -= 1 << 21
        page = ((a + i * 4) & ~0xFFF) + (imm << 12)
        for j in range(i + 1, min(i + 4, len(w))):
            y = w[j]
            if (y & 0xFFC00000) == 0x91000000 and ((y >> 5) & 31) == rd:
                out.append((a + j * 4, y, page + ((y >> 10) & 0xFFF)))
                break
    return out


def _add_shift(y, delta):
    """add xN,xN,#imm -> add xN,xN,#imm+delta (в пределах той же страницы)."""
    lo = ((y >> 10) & 0xFFF) + delta
    return None if lo > 0xFFF else (y & ~(0xFFF << 10)) | (lo << 10)


def find_values(lib, syms):
    """Адреса данных движка для offsets.ini: [(ключ, адрес)], заметки."""
    values, notes = [], []
    # Буфер пути приложения (char[0x410]): его пишет только SetApplicationPath,
    # а читают GetApplicationPath -> Application.dataPath, папка данных и
    # streamingAssetsPath. Загрузчик кладёт туда путь «APK» (main.c).
    fn = syms.get("_ZNK10FileSystem18SetApplicationPathEPKc")
    hits = _adrp_add(lib, *fn) if fn else []
    if fn and len(hits) == 1 and 0x52808202 in _words(lib, *fn):     # mov w2,#0x410
        values.append(("unity.app_path_buf", hits[0][2]))
    else:
        notes.append("app_path_buf: SetApplicationPath не опознан -- dataPath будет пустым")

    # TimeManager::Update(double): вход и тело для перехвата часов (main.c,
    # nx_install_time_fix). Пролог: ldr x8,[x0,#0xc8]; ldr w9,[x0,#0xd0];
    # ldrb w10,[x0,#0xf8]; ... cbz w10,<тело>; ret. Тело начинается с
    # ldr d2,[x0,#0xe8] (точка отсчёта). Раскладку полей проверяем по словам.
    fn = syms.get("_ZN11TimeManager6UpdateEd")
    body = None
    if fn:
        w = _words(lib, fn[0], 0x30)
        if list(w[:3]) == [0xF9406408, 0xB940D009, 0x3943E00A]:
            for i, x in enumerate(w):
                if (x & 0xFF00001F) == 0x3400000A:                     # cbz w10
                    t = _cond_target(x, fn[0] + i * 4)
                    if _words(lib, t, 4)[0] == 0xFD407402:              # ldr d2,[x0,#0xe8]
                        body = t
                    break
    if body:
        values += [("unity.tm_update", fn[0]), ("unity.tm_update_body", body)]
    else:
        notes.append("tm_update: пролог TimeManager::Update не опознан -- часы движка не перехватить")
    gtm = syms.get("_Z14GetTimeManagerv")
    if gtm:
        values.append(("unity.get_time_manager", gtm[0]))
    return values, notes


def find_patches(lib, syms):
    """Заплатки движка по символам и шаблонам инструкций (см. docs/STATUS.md).
    Возвращает список (имя, адрес, было, стало) и (имя, адрес, значение) для poke8."""
    patches, pokes, notes = [], [], []

    # 1. Время: AndroidVSync ждёт Choreographer, которого нет. tbnz на запасную
    #    ветку с bl GetTimeSinceStartup превращаем в безусловный переход.
    fn = syms.get("_ZN12AndroidVSync38WaitForLastPresentationAndGetTimestampEv")
    gts = syms.get("_Z19GetTimeSinceStartupv")
    if fn and gts:
        a, sz = fn
        w = _words(lib, a, sz)
        for i, x in enumerate(w):
            if (x & 0x7F000000) == 0x37000000:              # tbnz
                pc = a + i * 4
                imm = (x >> 5) & 0x3FFF
                if imm & 0x2000:
                    imm -= 0x4000
                t = pc + imm * 4
                k = (t - a) // 4
                if 0 <= k < len(w) and _bl_target(w[k], t) == gts[0]:
                    patches.append(("vsync_time", pc, x, _b(pc, t)))
                    break
        else:
            notes.append("vsync_time: tbnz на bl GetTimeSinceStartup не найден")

    # 2. FMOD: AudioManager::InitNormal выбирает вывод (21 AudioTrack); mov w1,wN
    #    перед bl FMOD::System::setOutput -> mov w1,#22 (OpenSL).
    fn = syms.get("_ZN12AudioManager10InitNormalEb15FMOD_OUTPUTTYPE")
    so = syms.get("_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE")
    if fn and so:
        a, sz = fn
        w = _words(lib, a, sz)
        done = False
        for i, x in enumerate(w):
            if _bl_target(x, a + i * 4) == so[0]:
                for j in range(i - 1, max(i - 8, 0), -1):
                    if (w[j] & 0xFFE0FFFF) == 0x2A0003E1:   # mov w1, wN
                        patches.append(("fmod_output", a + j * 4, w[j], 0x528002C1))
                        done = True
                        break
                break
        if not done:
            notes.append("fmod_output: mov w1,wN перед setOutput не найден")

    # 3. FMOD OpenSL: проверка геометрии буфера -- lsr wX,wX,#1; cmp; b.ls -> b.
    fn = syms.get("_ZN4FMOD12OutputOpenSL4initEijPiiP17FMOD_SOUND_FORMATiiPv")
    if fn:
        a, sz = fn
        w = _words(lib, a, sz)
        for i, x in enumerate(w):
            if (x & 0xFFFFFC00) == 0x53017C00 and ((x >> 5) & 31) == (x & 31):   # lsr wX,wX,#1
                for j in range(i + 1, min(i + 4, len(w))):
                    if (w[j] & 0xFF00001F) == 0x54000009:                       # b.ls
                        pc = a + j * 4
                        patches.append(("fmod_bufgeom", pc, w[j], _b(pc, _cond_target(w[j], pc))))
                        break
                break
        else:
            notes.append("fmod_bufgeom: шаблон не найден")

    # 3b. FrameTimeTracker (Choreographer через Java Looper/Handler): конструктор
    #     ждёт ответа от Java-потока, которого нет, -- первая зависшая версия
    #     порта стояла в нём после 2-го кадра. WaitVSync ждёт счётчик кадров
    #     того же Choreographer. Всё это -- ret в начале функции; WaitVSync при
    #     этом возвращает свой аргумент (x0), то есть «дождались».
    for nm, key in (("_Z22EnableFrameTimeTrackerv", "frametime_enable"),
                    ("_Z23DisableFrameTimeTrackerv", "frametime_disable"),
                    ("_Z9WaitVSyncl", "wait_vsync")):
        fn = syms.get(nm)
        if fn:
            first = _words(lib, fn[0], 4)[0]
            patches.append((key, fn[0], first, 0xD65F03C0))
        else:
            notes.append(f"{key}: нет символа {nm}")

    # 4. Swappy: флаг принудительного отключения.
    sf = syms.get("_ZN6SwappyL14s_ForceDisableE")
    if sf:
        pokes.append(("swappy_force_disable", sf[0], 1))

    # 5. streamingAssetsPath на Android -- «jar:file://» + путь APK + «!/assets».
    #    APK у нас нет, и jar-адрес Addressables отдаёт UnityWebRequest, который
    #    ищет файл внутри zip. Делаем обычный путь «<путь APK>/assets» (его
    #    загрузчик сводит к папке игры): префикс переводим на NUL в конце
    #    «jar:file://», то есть на пустую строку, а «!/assets» -- на «/assets».
    fn = syms.get("_Z22GetStreamingAssetsPathv")
    found = {}
    for pc, y, t in (_adrp_add(lib, *fn) if fn else []):
        s = lib.read_va(t, 12)
        if s.startswith(b"jar:file://\0"):
            found["streaming_jar"] = (pc, y, _add_shift(y, 11))
        elif s.startswith(b"!/assets\0"):
            found["streaming_bang"] = (pc, y, _add_shift(y, 1))
    if len(found) == 2 and all(v[2] is not None for v in found.values()):
        patches += [(k, pc, y, ny) for k, (pc, y, ny) in sorted(found.items())]
    else:
        notes.append(f"streaming_assets: найдено {sorted(found)} -- путь останется jar:")
    return patches, pokes, notes


def offsets_lines(lib, syms):
    patches, pokes, notes = find_patches(lib, syms)
    lines = [f"patch.unity.{n} = 0x{a:x} 0x{f:08x} 0x{t:08x}" for n, a, f, t in patches]
    lines += [f"poke8.unity.{n} = 0x{a:x} {v}" for n, a, v in pokes]
    regions = region_patch_set(lib, syms)
    if len(regions) != 21:
        notes.append(f"регионы: найдено {len(regions)} мест вместо 21 -- заплатку зернистости НЕ пишу")
    else:
        lines += [f"patch.unity.region{i:02d} = 0x{a:x} 0x{f:08x} 0x{t:08x}"
                  for i, (k, a, f, t, n) in enumerate(regions)]
    for key, name in JNI.items():
        if key.startswith(("unity.native", "unity.shader_", "unity.quality_", "unity.app_", "unity.screen_", "unity.umbra_")) or key == "unity.initJni":
            if name in syms:
                lines.append(f"{key} = 0x{syms[name][0]:x}")
    values, vnotes = find_values(lib, syms)
    lines += [f"{k} = 0x{v:x}" for k, v in values]
    return lines, notes + vnotes



# ------------------------------------------- зернистость регионов 256 -> 64 МБ
#
# Распределитель Unity резервирует адресное пространство регионами по 2^28
# байт; загрузчику на Свитче столько окон не хватает (на 10-м резерве OOM).
# Порт Papers, Please менял зернистость на 2^26 в 21 инструкции функций
# менеджера памяти и проверил это на железе. Здесь то же самое правилами, а не
# адресами: сдвиги 28->26 (и 40->38), константы 0x10000000->0x4000000, маски
# с границей на бите 28/36 -> 26/34. Ожидаемый итог для этих функций -- 21 место.

MEM_FUNCS = ("VirtualAllocator", "LowLevelVirtualAllocator", "LocalLowLevelAllocator",
             "DynamicHeapAllocator", "BucketAllocator", "TLSAllocator")


def _decode_bitmask64(n, immr, imms):
    if not n:
        return None
    ones = imms + 1
    if ones >= 64:
        return None
    v = (1 << ones) - 1
    return ((v >> immr) | (v << (64 - immr))) & 0xFFFFFFFFFFFFFFFF if immr else v


def _encode_bitmask64(mask):
    """Обратное для непрерывного (с поворотом) набора единиц, N=1."""
    for immr in range(64):
        rot = ((mask << immr) | (mask >> (64 - immr))) & 0xFFFFFFFFFFFFFFFF if immr else mask
        if rot and (rot & (rot + 1)) == 0:          # вида 0..01..1
            ones = rot.bit_length()
            if ones < 64:
                return immr, ones - 1
    return None


def _region_rewrite(w):
    """Новое слово для инструкции с константой региона 2^28 или None."""
    rd_rn = w & 0x3FF
    if (w & 0xFFFFFFE0) == 0x12BE0000:               # movn w, #0xf000, lsl16  (0x0fffffff)
        return ("movn", 0x12BF8000 | (w & 31))
    if (w & 0xFFFFFFE0) == 0x52A20000:               # mov  w, #0x10000000
        return ("movz", 0x52A08000 | (w & 31))
    if (w & 0xFFFFFFE0) == 0xF2A20000:               # movk x, #0x1000, lsl16
        return ("movk", 0xF2A08000 | (w & 31))
    if (w & 0xFFC00000) == 0xD3400000:               # ubfm x (lsr / ubfx)
        immr, imms = (w >> 16) & 63, (w >> 10) & 63
        if immr == 28:
            nimms = imms if imms == 63 else imms - 2
            return ("lsr28" if imms == 63 else "ubfx28", 0xD3400000 | (26 << 16) | (nimms << 10) | rd_rn)
        if immr == 40 and imms == 63:
            return ("lsr40", 0xD3400000 | (38 << 16) | (63 << 10) | rd_rn)
    if (w & 0xFF200000) == 0xCB000000 and ((w >> 22) & 3) == 0 and ((w >> 10) & 63) == 28:
        return ("sub_lsl28", (w & ~(63 << 10)) | (26 << 10))   # sub x, x, x, lsl #28
    if (w & 0x9F800000) == 0x92000000 or (w & 0x9F800000) == 0xB2000000:   # and/orr x, #imm (64 бит)
        n, immr, imms = (w >> 22) & 1, (w >> 16) & 63, (w >> 10) & 63
        m = _decode_bitmask64(n, immr, imms)
        if m is None:
            return None
        low = (m & -m).bit_length() - 1                 # начало набора единиц
        if low in (28, 36):
            nm = m & ~((1 << (low - 2)) - 1) | (((1 << low) - 1) ^ ((1 << (low - 2)) - 1))
            enc = _encode_bitmask64(nm)
            if enc:
                kind = "and_mask" if (w & 0x60000000) == 0 else "orr_mask"
                return (kind, (w & ~(0x1FFF << 10)) | (1 << 22) | (enc[0] << 16) | (enc[1] << 10))
    return None


def find_region_patches(lib, syms):
    out = []
    text = lib.section(".text")
    t0, t1 = text["addr"], text["addr"] + text["size"]
    for name, (a, sz) in sorted(syms.items(), key=lambda kv: kv[1][0]):
        if sz <= 0 or not (t0 <= a < t1) or not any(k in name for k in MEM_FUNCS):
            continue
        w = _words(lib, a, sz)
        for i, x in enumerate(w):
            r = _region_rewrite(x)
            if r:
                out.append((r[0], a + i * 4, x, r[1], name))
    return out


def region_patch_set(lib, syms):
    """Ровно тот набор, что проверен на железе в порте Papers, Please (21 место):
    плюс GetAllocatorContainingPtr; без «не меньше 256 МБ» в конструкторе
    BucketAllocator и без сдвига в TLSAllocator (там менялась только константа)."""
    global MEM_FUNCS
    MEM_FUNCS = MEM_FUNCS + ("GetAllocatorContainingPtr",)
    seen, out = set(), []
    for kind, a, f, t, name in find_region_patches(lib, syms):
        if a in seen:
            continue
        if "BucketAllocatorC" in name:
            continue
        if "TLSAllocator" in name and kind == "lsr28":
            continue
        seen.add(a)
        out.append((kind, a, f, t, name))
    return out


if __name__ == "__main__":
    main()
