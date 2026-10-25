# FSR 3.1 frame generation / deko3d

Экспериментальная игровая интеграция r43. На Switch проверен полный граф
AMD на статичных и движущихся тестовых кадрах. Проверка игрового вывода,
задержки, нагрузки и переходов между сценами ещё требуется.

Первый игровой лог r43 не подтверждает запуск native graph: после создания
сессии нет отправки/обработки кадров. В r44 managed барьер worldFrame заменён
событием готовности в очереди рендера после захвата depth/motion; добавлены
сообщения о пропущенном захвате и невалидных параметрах кадра. Полный игровой
вывод r44 ещё требует проверки. Подробнее: docs/PERF-r44.md.

Перенесены 18 официальных compute passes AMD Optical Flow и Frame Interpolation
из SDK v1.1.3 (54fbaafdc34716811751bea5032700e78f5a0f33) в DKSH для Maxwell.
Скомпилированы все 8 комбинаций low-res motion / inverted depth / jitter:
144 успешных компиляции. Инверсия глубины действительно меняет reconstruction
shader. Это подтверждает компиляцию, не правильность кадров на GPU.

Адаптеры: ARB_shader_ballot/readInvocationARB вместо KHR subgroup intrinsics;
раздельные Vulkan descriptors превращены в плотные deko bindings; 13 UAV mip
views заменены coherent SSBO с RGBA16F half packing. Для SPD backend должен
после dispatch скопировать линейные mip ranges в sampled pyramid texture.
Математика AMD сохранена, FP32/SDR; половинная арифметика/HDR не заявлены.

`tools/build_fsr31_deko.py --cc <gcc> --uam <uam> --output <directory>`
проверяет SHA всех vendor files, компилирует и проверяет заголовки DKSH,
сохраняет bindings/workgroup/registers/scratch в manifest.json.
Флаги: --low-res-motion-vectors 0|1, --inverted-depth, --jitter-motion-vectors.
`tools/test_fsr31_deko.py` проверяет контракты преобразования descriptors.

`tools/build_fsr31_deko_probe.py --shaders <compiled-directory> --output <new-directory>`
собирает отдельный fsr31-deko-probe.nro с шейдерами в RomFS. Он проверяет
на GPU полные/частичные wave masks, packed mip bounds/read-after-write,
настоящий AMD prepare_luma на 4096 пикселях против CPU. GPU fence ограничен
двумя секундами. Лог: sdmc:/fsr31-deko-probe.log. Запуск через hbmenu,
PLUS — выход. Это диагностическое приложение, а не игровой установщик.
v9 прошёл на Switch: общая память GLES/deko, весь граф SDK, кадр после сброса,
два статичных кадра с историей и три движущихся промежуточных кадра.
Центр движущегося объекта совпал с серединой между исходными положениями;
проверка отвергает простой повтор текущего кадра.

`deko/sdk_backend.cpp` связывает неизменённые CPU-планировщики AMD с
`deko/gpu_backend.cpp`: ресурсы, пайплайны, bindings, barriers и отправка GPU
команд. `loader/source/nx_fg_interop.cpp` использует persistent coherent PBO,
зарегистрированный как deko memory block. GLES texture ID не является deko
Image. Пиксели между API переносятся GPU; CPU ожидает ограниченные fences.

`unity/Assets/NxPort/Fsr/NxFrameGeneration.cs` захватывает цвет, глубину и
motion vectors. Глубина float32 и motion half2 побитно упаковываются в RGBA8
и копируются в R32/RG16 deko images. Native callback выполняет optical flow,
prepare и frame interpolation. EGL presenter выводит промежуточный и текущий
кадры, не выполняя дополнительную игровую логику. История сбрасывается при
смене сцены, разрыве frame ID, телепорте, повороте/изменении FOV камеры.

Аппаратный тест подтверждает математику тестовых кадров, но не гарантирует
повышение FPS в игре. Нужно измерить стоимость на 720p и проверить present.
HUD пока интерполируется вместе с изображением. Основной рендер Unity остаётся
GLES; через deko выполняется генерация кадров. Игровой профиль пока только
SDR/FP32, standard depth и low-resolution motion vectors.

В r43 отдельный параметр `frame_generation`, по умолчанию 1, и переключатель
Frame generation (TEST) в панели SWITCH. Ошибка backend отключает генерацию
и сохраняет обычный вывод. При просроченном GPU fence ресурсы изолируются до
выхода из процесса, чтобы не освобождать память, к которой ещё обращается GPU.
Установленный r42 сам по себе этой функциональности не получает.
