# Сборка порта

Здесь загрузчик, C#-слой и инструменты. Оригинальные файлы и код ULTRAKILL,
Unity-проект игры, ключи консоли и готовые библиотеки игры в Git не входят.
Для подготовки проекта нужна собственная копия игры.

Рабочая сборка: **Unity 2022.3.29f1**, Android ARM64 / IL2CPP, OpenGL ES 3.
Другая версия движка требует повторного вывода и проверки смещений.
Полностью автоматического сценария одной командой пока нет.

## Загрузчик

Нужны devkitA64/libnx и portlibs: SDL2, Mesa, nouveau, libpng, zlib, libjpeg-turbo.
В окружении devkitPro:

```sh
make -C loader -j4
```

Результат: `loader/ultrakill_nx.nro`. Основные заплатки движка — в
`loader/source/main.c`, совместимость Android — в shim/JNI/OpenSL файлах.

## Unity и данные

1. Подготовить проект из своей копии ULTRAKILL через VanityReprised / AssetRipper.
   Сохранить исходные имена игровых сборок.
2. Скопировать `unity/Assets/NxPort` и `unity/Assets/NxPortEditor` в проект.
3. Открыть в Unity 2022.3.29f1 с Android Build Support, NDK и OpenJDK.
4. Выполнить `NxFsrBuild.Export` через `-executeMethod` в batchmode:
   GPU-проба FSR, отдельный Android shader bundle, проверка кнопок через
   настоящий Input System, затем экспорт Gradle-проекта.
5. Для backend GC r23 применить `tools/patch_gc_horizon.py` к экспортированному
   `IL2CPP/external/bdwgc/pthread_stop_world.c`. Подробнее: [GC-r23.md](GC-r23.md).
6. Скомпилировать экспортированный C++ через его `il2cpp.exe --compile-cpp`:
   Android, arm64, Release, NDK этой версии Unity. Пути берутся из
   экспортированного `unityLibrary/build.gradle`.

Сцена редактора не заменяет данные оригинальной игры. Для данных используются
`tools/prep_data.py`, `tools/port_all.py`, `tools/port_files.py`, `tools/fix_catalog.py`.
Шейдеры требуют преобразования в GLES3. `tools/dxbc2glsl/dxbc2glsl.cpp` собирается
с отдельной зависимостью HLSLcc; готовый exe сюда не включён.

`tools/unity_syms.py` выводит смещения движка по символам.
`tools/derive_offsets.py` выводит GC, JavaVM и managed-перехваты по совпадающим
`libil2cpp.so` / `libil2cpp.sym.so`. Build-id должен совпадать.

```sh
python tools/derive_offsets.py --lib libil2cpp.so --sym libil2cpp.sym.so -o il2cpp.ini
```

Для видео нужны ffmpeg/ffprobe:

```sh
python tools/prepare_video.py input.webm output.nxv
```

Файлы кладутся в `assets/VideosNX/` с именами исходных VideoClip/файлов:
`splashscreen`, `5-S end video new`, `fuckin_finland`, `uk_museum_ingame`.
Встроенные ролики предварительно извлекаются из ресурсов videos.bundle.

`tools/pack_switch.py` собирает базовую папку для SD; VideosNX и лицензии кодека
добавляются отдельно. Библиотека, offsets и metadata обновляются вместе;
runtime кэш `il2cpp/Metadata/global-metadata.dat` тоже обновляется или пересоздаётся.

## Форвардер и проверки

`forwarder/` содержит nx-hbloader с путём к нашему NRO. После сборки NSO/NPDM
`tools/make_forwarder.sh` создаёт NSP через hacBrewPack и локальные ключи.
Ключи не включаются в репозиторий или архив сборки.

`tools/test_*.py` проверяют смещения, память, синхронизацию и диагностику.
Часть тестов компилирует настоящий C-код через host GCC. Тест снимков стеков
использует Windows VirtualQuery и защищённые страницы:

```sh
python tools/test_diag_snapshot.py --cc C:/path/to/gcc.exe
```

Тесты на ПК не заменяют проверку уровней на Switch.

FSR bundle из `Build/NxPortAndroid/nx_fsr.bundle` копируется в
`assets/NxPort/nx_fsr.bundle`. Заголовки AMD включены с лицензиями.
После C#-изменений заново проверить все managed/video offsets и обе metadata.

Для диагностики повторяющихся исключений перед compile-cpp применить
`python tools/patch_exception_trace.py path/to/IL2CPP/libil2cpp/vm/Exception.cpp`.
Патч сохраняет исходник, проверяет якорь и вызывает необязательный callback
до разворачивания стека. Без callback поведение Android сохраняется.
Umbra offsets выводятся `unity_syms.py`; NRO проверяет сигнатуру и цели BL.
