#!/usr/bin/env python3
"""make_frame_table.py -- таблица фаз player loop для замера кадра (nx_frame.c).

Каждая фаза кадра Unity (FixedUpdate.PhysicsFixedUpdate, Update.ScriptRunBehaviourUpdate,
PostLateUpdate.FinishFrameRendering ...) -- функция-регистратор вида
`...::<Фаза><Имя>Registrator::Forward()`. Указатели на них лежат в общей таблице
колбэков; ExecutePlayerLoop вызывает их через неё (двойное разыменование), поэтому
загрузчик подменяет значения в таблице обёртками с замером, не трогая код движка.

Адрес таблицы берётся из начала InitPlayerLoopCallbacks: adrp + ldr из GOT.

    python make_frame_table.py --lib <игровая libunity.so> --sym <libunity.sym.so> -o loader/source/nx_frame_table.h
"""
from pathlib import Path
import argparse
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from derive_offsets import Elf, sx  # noqa: E402

PHASES = ["PostLateUpdate", "PreLateUpdate", "Initialization", "EarlyUpdate",
          "FixedUpdate", "TimeUpdate", "PreUpdate", "Update"]
SUFFIX = "7ForwardEv"


def local_name(name):
    """Имя локального класса перед ::Forward(): <длина><имя>Registrator в конце."""
    if not name.endswith("Registrator" + SUFFIX):
        return None
    s = name[:-len(SUFFIX)]
    for m in re.finditer(r"\d+", s):
        if m.end() < len(s) and not s[m.end()].isdigit() and int(m.group()) == len(s) - m.end():
            return s[m.end():]
    return None


def entries(sym):
    out = []
    for name, (value, size) in sym.symbols().items():
        ident = local_name(name)
        if not ident or not size:
            continue
        base = ident[:-len("Registrator")]
        for phase in PHASES:
            if base.startswith(phase) and len(base) > len(phase) and base[len(phase)].isupper():
                out.append((value, f"{phase}.{base[len(phase):]}"))
                break
    # Одинаковые тела линкер мог слить в один адрес: тогда имя общее, через «/».
    merged = {}
    for value, name in sorted(set(out)):
        merged.setdefault(value, []).append(name)
    return [(v, "/".join(n)) for v, n in sorted(merged.items())]


# Функции внутри PostLateUpdate.FinishFrameRendering (PlayerRender) для замера по
# входу (nx_hook.c). Только с аргументами в регистрах (<= 8 целых): переходник
# сдвигает стек. Только те, что зовёт главный поток (запись входа не атомарна).
HOOKS = [
    ("RenderCameras", "_ZN13RenderManager13RenderCamerasEiPFvvES1_"),
    ("RenderOffscreenCameras", "_ZN13RenderManager22RenderOffscreenCamerasEv"),
    ("Camera::CustomRender", "_ZN6Camera12CustomRenderER11CullResultsR17ShaderPassContextPK21CameraRenderingParamsNS_10RenderFlagEPNS_21PerformRenderFunctionEPFvvESB_"),
    ("CullScene", "_Z9CullSceneR11CullResults"),
    ("DoRenderLoop", "_Z12DoRenderLoopR10RenderLoop13RenderingPathR11CullResultsR14ShadowMapCache"),
    ("RenderLoop::PrepareFrame", "_ZN10RenderLoop12PrepareFrameEb"),
    ("DoForwardShaderRenderLoop", "_Z25DoForwardShaderRenderLoopRK17RenderLoopContextRN4core6vectorI16RenderObjectDataLm0EEERK11CullResultsbbbN6Camera10RenderFlagE"),
    ("Forward::StartRenderJobs", "_ZN23ForwardShaderRenderLoop15StartRenderJobsER8JobFencebbR17ShaderPassContext"),
    ("Forward::PerformRendering", "_ZN23ForwardShaderRenderLoop16PerformRenderingER8JobFencePK11ActiveLightRK13ShadowJobDatabbbN6Camera10RenderFlagE"),
    ("ImageFilters::DoRender", "_ZNK12ImageFilters8DoRenderER17ShaderPassContextP13RenderTextureS3_RKN4core6vectorI11ImageFilterLm0EEE"),
    ("UI::RenderOverlays", "_ZN2UI13CanvasManager14RenderOverlaysEi"),
    ("GfxDeviceClient::PresentFrame", "_ZN15GfxDeviceClient12PresentFrameE17ShaderChannelMask"),
    # r73: остаток Camera::CustomRender у Main Camera (2.2 мс в r71) -- не отсечение и не цикл
    ("Camera::UpdateDepthTextures", "_ZN6Camera19UpdateDepthTexturesERK11CullResultsRK19SharedRendererSceneNS_10RenderFlagE"),
    ("SyncFenceCullResults", "_Z20SyncFenceCullResultsR11CullResults"),
    ("DispatchGeometryJobs", "_Z20DispatchGeometryJobsPN4core6vectorIP12BaseRendererLm0EEERK33RendererCullingCallbackProperties"),
    ("Camera::DoRenderPostLayers", "_ZN6Camera18DoRenderPostLayersEPK11CullResultsR17ShaderPassContextNS_10RenderFlagEPFvvE"),
    ("RenderImageFilters", "_Z18RenderImageFiltersR10RenderLoopb"),
    ("ExecuteCommandBuffers", "_ZNK19RenderEventsContext21ExecuteCommandBuffersEijR17ShaderPassContextR15RenderNodeQueuePN9profiling6MarkerEi"),
    ("Camera callbacks", "_ZN14CallbackArray1IP6CameraE6InvokeES1_"),
    ("Camera::SetupRender", "_ZN6Camera11SetupRenderER17ShaderPassContextRK21CameraRenderingParamsNS_10RenderFlagE"),
    ("Camera::PreMultiCustomRender", "_ZN6Camera20PreMultiCustomRenderER11CullResultsNS_10RenderFlagEb"),
    ("Camera::PostMultiCustomRender", "_ZN6Camera21PostMultiCustomRenderENS_10RenderFlagEb"),
    # r74: в r73 у Main Camera 3.5 из 7.4 мс всё ещё «без замера». Внутри CustomRender
    # зовётся RenderManager::UpdateAllRenderers (сам с bl в прологе) -- его дети:
    ("RendererUpdateManager::UpdateAll", "_ZN21RendererUpdateManager9UpdateAllER13RendererScene"),
    ("RendererUpdateManager::OncePerFrame", "_ZN21RendererUpdateManager18OncePerFrameUpdateER13RendererScene"),
    ("ParticleSystem::EndUpdateAll", "_ZN14ParticleSystem12EndUpdateAllEv"),
    ("ParticleSystem::SyncJobs", "_ZN14ParticleSystem8SyncJobsEb"),
    ("LightManager::UpdateAllLightTransformData", "_ZN12LightManager27UpdateAllLightTransformDataEv"),
    # сбор списка объектов к рендеру и колбэки скриптов (OnPreRender, OnWillRenderObject ...)
    ("CullResults::GetOrCreateSharedRendererScene", "_ZN11CullResults30GetOrCreateSharedRendererSceneEv"),
    ("PrepareRenderNodesOnMainThread", "_ZL40PrepareRenderNodesOnMainThreadDeprecatedR35RenderNodeQueuePrepareThreadContext"),
    ("PrepareRenderNodesQueueToMainThread", "_ZL35PrepareRenderNodesQueueToMainThreadR35RenderNodeQueuePrepareThreadContext"),
    ("CopyNodesIntoJobGaps", "_ZL20CopyNodesIntoJobGapsP29RenderNodeQueuePrepareContext"),
    ("RenderNodeQueuePrepareContext()", "_ZN29RenderNodeQueuePrepareContextC1Ev"),
    ("ScriptingInvocation::Invoke", "_ZN19ScriptingInvocation6InvokeEP21ScriptingExceptionPtrb"),
    ("UI::EmitGeometryForCamera", "_ZN2UI13CanvasManager21EmitGeometryForCameraEP6Camera"),
    ("UI::EmitWorldScreenspaceCameraGeometry", "_ZN2UI13CanvasManager34EmitWorldScreenspaceCameraGeometryENS0_22CanvasCameraRenderTypeEi"),
    ("Camera::IsValidToRender", "_ZNK6Camera15IsValidToRenderEv"),
    ("Camera::ExtractCameraRenderingParams", "_ZNK6Camera28ExtractCameraRenderingParamsEv"),
    # r78: в тяжёлом бою r77 физика -- 8.2 мс за кадр (2.95 шага). Что внутри шага:
    # симуляция PhysX, ожидание её потоков (fetchResults), колбэки столкновений в
    # скрипты (ProcessReports) или перенос поз в трансформы.
    ("PhysicsManager::Simulate", "_ZN14PhysicsManager8SimulateEif"),
    ("PhysicsManager::SyncTransforms", "_ZN14PhysicsManager14SyncTransformsEv"),
    ("PhysicsManager::SyncBatchQueries", "_ZN14PhysicsManager16SyncBatchQueriesEv"),
    ("PhysicsScene::Simulate", "_ZN12PhysicsScene8SimulateEf"),
    ("PhysicsScene::ProcessReports", "_ZN12PhysicsScene14ProcessReportsEv"),
    ("PhysicsScene::StoreSimulationResults", "_ZN12PhysicsScene22StoreSimulationResultsEv"),
    ("PhysicsScene::StoreInterpolationPoses", "_ZN12PhysicsScene23StoreInterpolationPosesER8JobFence"),
    ("PxScene::simulate", "_ZN5physx7NpScene17simulateOrCollideEfPNS_10PxBaseTaskEPvjbPKcNS_2Sc15SimulationStage4EnumE"),
    ("PxScene::fetchResults", "_ZN5physx7NpScene12fetchResultsEbPj"),
    ("PxScene::fetchResultsStart", "_ZN5physx7NpScene17fetchResultsStartERPKNS_19PxContactPairHeaderERjb"),
    ("PxScene::fetchResultsPostContactCallbacks", "_ZN5physx7NpScene32fetchResultsPostContactCallbacksEv"),
    ("PxScene::fetchResultsFinish", "_ZN5physx7NpScene18fetchResultsFinishEPj"),
]


def pc_relative(w):
    return ((w & 0x1F000000) == 0x10000000 or (w & 0x7C000000) == 0x14000000 or
            (w & 0xFF000010) == 0x54000000 or (w & 0x7E000000) == 0x34000000 or
            (w & 0x7E000000) == 0x36000000 or (w & 0x3B000000) == 0x18000000)


def hooks(lib, sym):
    syms, out = sym.symbols(), []
    for short, mangled in HOOKS:
        va = syms[mangled][0]
        words = struct.unpack("<4I", lib.read_va(va, 16))
        assert not any(pc_relative(w) for w in words), f"{short}: пролог зависит от PC"
        out.append((va, words, short))
    return out


def table_got(lib, sym):
    va = sym.symbols()["_Z23InitPlayerLoopCallbacksv"][0]
    adrp, ldr = struct.unpack("<II", lib.read_va(va, 8))
    assert (adrp & 0x9F000000) == 0x90000000, f"нет adrp в начале InitPlayerLoopCallbacks: {adrp:#x}"
    assert (ldr & 0xFFC00000) == 0xF9400000, f"нет ldr x из GOT: {ldr:#x}"
    rd = adrp & 31
    assert (ldr >> 5) & 31 == rd, "ldr берёт не тот регистр"
    imm = sx(((adrp >> 5) & 0x7FFFF) << 2 | ((adrp >> 29) & 3), 21) << 12
    page = (va & ~0xFFF) + imm
    return page + ((ldr >> 10) & 0xFFF) * 8


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--lib", required=True, type=Path)
    p.add_argument("--sym", required=True, type=Path)
    p.add_argument("-o", "--output", required=True, type=Path)
    a = p.parse_args()
    lib, sym = Elf(a.lib), Elf(a.sym)
    assert lib.build_id() and lib.build_id() == sym.build_id(), "build-id libunity и .sym.so не совпадают"
    got = table_got(lib, sym)
    items = entries(sym)
    lines = [
        f"/* nx_frame_table.h -- сгенерировано tools/make_frame_table.py; libunity build-id {lib.build_id()} */",
        "#pragma once",
        "#include <stdint.h>",
        f"#define NX_FRAME_BUILD_ID \"{lib.build_id()}\"",
        f"#define NX_FRAME_GOT 0x{got:x}u  /* слот GOT с адресом таблицы колбэков player loop */",
        "static const struct { uint32_t off; const char *name; } nx_frame_entries[] = {",
    ]
    lines += [f"  {{ 0x{v:x}u, \"{n}\" }}," for v, n in items]
    lines += ["};", ""]
    getname = sym.symbols()["_ZNK5Unity9Component7GetNameEv"][0]
    gn_words = struct.unpack("<2I", lib.read_va(getname, 8))
    lines += ["/* Unity::Component::GetName() const -- имя камеры для [frame-cam] */",
              f"#define NX_COMPONENT_GET_NAME 0x{getname:x}u",
              f"#define NX_COMPONENT_GET_NAME_W0 0x{gn_words[0]:08x}u",
              f"#define NX_COMPONENT_GET_NAME_W1 0x{gn_words[1]:08x}u", ""]
    lines += ["/* замер по входу (nx_hook.c): адрес, пролог для сверки, имя */",
              "static const struct { uint32_t off; uint32_t prologue[4]; const char *name; } nx_frame_hooks[] = {"]
    lines += [f"  {{ 0x{v:x}u, {{ {', '.join(f'0x{w:08x}u' for w in ws)} }}, \"{n}\" }},"
              for v, ws, n in hooks(lib, sym)]
    lines += ["};", ""]
    a.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"{a.output}: фаз {len(items)}, GOT 0x{got:x}")


if __name__ == "__main__":
    main()
