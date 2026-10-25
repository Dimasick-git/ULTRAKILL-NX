# FSR 3.1 Frame Generation: состояние переноса

20 октября 2026. Генерация кадров не реализована. Запрошенное поведение после
реализации: включать по умолчанию. В r41 генерации кадров нет.

## Проверено в коде

- `unity/Packages/fidelityfx.fsr/package.json`: Upscaler core assets.
  Реализаций AMD Optical Flow и Frame Interpolation в пакете нет.
- `unity/Assets/NxPort/Fsr/NxFsr3.cs`: Fsr3UpscalerContext, depth, motion
  vectors, reactive mask; текущая итоговая композиция включает HUD.
- `loader/source/imports.c::egl_SwapBuffers_log`: обычный EGL present;
  отдельной очереди интерполированных кадров нет.
- Загрузчик использует Mesa/GLES. AMD предоставляет FG swapchain для
  DX12/Vulkan; готового GLES backend нет. Это ограничение текущего порта,
  а не доказательство аппаратной невозможности.

## Что требуется

1. Перенести AMD Optical Flow и Frame Interpolation compute passes и их
   resource scheduling на GLES. Проверить subgroup operations, image
   bindings, форматы и барьеры.
2. Передавать два кадра без HUD, согласованные depth/motion, frame ID,
   параметры камеры; сбрасывать историю при смене сцены/RT и телепортации.
3. Выводить HUD и прицел отдельно поверх интерполированного мира.
4. Реализовать очередь present с GPU fences и владением EGL context.
   Дополнительный present не должен вызывать Unity Update или физику.
5. На Switch проверить нагрузку, память, задержку, suspend/resume,
   переходы сцен и восстановление обычного present при отказе.

Готовность: на движущемся тесте действительно создан промежуточный кадр,
HUD резкий, present timestamps равномерны, число игровых обновлений прежнее.
До готовности этого пути включаемого режима FG не существует.

Источник: [AMD FSR 3.1.4 integration](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/),
разделы API requirements, Frame Generation Swapchain, Async workloads,
UI Composition. Проверено 20 октября 2026.

## Начат deko3d backend

В experimental/framegen перенесены и скомпилированы 18 AMD compute passes
в 8 профилях (144 DKSH). Собран отдельный GPU conformance probe.
[Подробности](../experimental/framegen/README.md). В игровой runtime пока не включено.
