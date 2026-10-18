# Устройство порта

| Слой | Ответственность |
| :-- | :-- |
| `forwarder/` | HOME-запуск NRO в режиме полноценного приложения |
| `loader/source/` | Загрузка ELF, Android/JNI/libc/OpenSL совместимость, окно и GLES |
| `nx_gc.c` + `patch_gc_horizon.py` | Остановка Horizon-потоков и корни для Boehm |
| `nx_umbra.c` | Проверка NULL при загрузке данных окклюзии Unity |
| `nx_exception_trace.c` | Ограниченные образцы до выброса NRE |
| `nx_cpu.c`, `diag.c` | Реальные CPU-маски, измерения потоков и диагностика |
| `nx_bridge.c` + `NxBridge.cs` | Ввод, гироскоп, конфигурация и видео между native и C# |
| `unity/Assets/NxPort` | Input System, гироприцел, видео и FSR-композиция |
| `unity/Assets/NxPortEditor` | Проверки ввода, GPU-проба FSR и Android Export |
| `tools/` | Подготовка данных/шейдеров, offsets, проверки и упаковка |

Движок и IL2CPP собраны для Android ARM64; загрузчик обслуживает платформенные
вызовы на Horizon. Графика идёт через GLES/Mesa/nouveau. Видео использует
подготовленные JPEG/PCM sidecar-файлы; декодирование кадров вынесено в поток.

Публичный Git содержит код порта и инструменты. Исходный Unity-проект игры,
готовые игровые библиотеки, личные данные и ключи в Git не включаются.

Граница совместимости сборки: NRO, libil2cpp.so, offsets, metadata, shader bundle.
При изменении C# заново экспортируются и компилируются IL2CPP и metadata,
проверяется build-id пары lib/sym и повторно выводятся managed/video offsets.
Чистая установка не содержит runtime-кэш il2cpp; обновление заменяет его metadata
или удаляет кэш целиком. Данные игры и сохранения не используются как тестовые фикстуры CI.
