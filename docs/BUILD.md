# Сборка порта

Здесь загрузчик, C#-слой и инструменты. Оригинальные файлы и код ULTRAKILL,
Unity-проект игры, ключи консоли и готовые библиотеки игры в Git не входят.
Для подготовки проекта нужна собственная копия игры.

Рабочая сборка: **Unity 2022.3.29f1**, Android ARM64 / IL2CPP, OpenGL ES 3.
Другая версия движка требует повторного вывода и проверки смещений.
Полностью автоматического сценария одной командой пока нет.

Для r83 сначала подготовьте 13 TSV-таблиц и Android-бандл шрифта по
[инструкции локализации](LOCALIZATION.md). `stage_full_game.py` принимает
`--language-dir`, `--font-bundle`, `--font-license`; при `--version r83`
таблицы всех 13 переводов, CJK-бандл и его лицензия обязательны.
Упаковщик полного NSP повторно проверяет их наличие.

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
4. Выполнить `NxWeaponSlotsCheck.Export` через `-executeMethod` в batchmode:
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

После exception trace применить `python tools/patch_nullcheck_site.py path/to/IL2CPP`.
Это требует полной перекомпиляции зависимых C++ единиц. Все три runtime-патча
(GC, exception trace, nullcheck source) применяются после Export, до compile-cpp.

После каждого экспорта применить также патч optional desktop mouse:

```sh
python tools/patch_mouse_scroll.py path/to/Source/il2cppOutput
python tools/test_mouse_scroll.py path/to/Source/il2cppOutput/Assembly-CSharp__13.cpp --cc g++
```

Это адресный guard в GunControl.Update: при Mouse.current == NULL прокрутка
равна нулю, выполнение продолжается к логике геймпада. Патч проверяет точный
метод и продолжение, завершается ошибкой при несовпадении экспорта. Другие
NullCheck сохраняются. Метаданные этого исправления не меняются; смещения
из нового libil2cpp.sym.so всё равно нужно вывести заново.

## Полный установщик NSP (2.0.0 TEST1 / r37)

Перед экспортом один раз примените `tools/patch_switch_prompts.ps1` к локальной
`Assets/Plugins/Assembly-CSharp.dll`. Параметры: `-Assembly` — этот DLL,
`-InputSystemAssembly` — соседний `Unity.InputSystem.dll`, `-Cecil` —
`Editor/Data/il2cpp/build/deploy/Mono.Cecil.dll` из установленной Unity.
Инструмент сохраняет резервную копию, проверяет структуру метода и допускает
повторный запуск. Изменённый DLL игры в Git не добавляется.

`NxFsr3Build.Export` сначала проверяет реальные UI-кнопки, drag, отмену,
масштабирование координат, затем запускает проверки FSR/геймпада и экспорт.
`NxInputCheck` проверяет подсказки на настоящем resolver игры, включая remap.
После экспорта обязательны обычные патчи GC/exception/nullcheck/mouse. Для
диагностической TEST1 также применяется `tools/patch_cpu_scopes.py` к каталогу
`Il2CppOutputProject/Source/il2cppOutput`, затем собирается IL2CPP. Это RAII-замеры
восьми методов; ранние выходы и исключения сохраняют исходное поведение.
Первый вызов включает разрешение адреса диагностического callback. Метрики
измеряют включительное прошедшее время, а не независимые доли CPU.

`tools/stage_full_game.py` объединяет неизменяемые Addressables/видео из полной
рабочей сборки с новыми библиотеками, NRO и метаданными кода. Все стартовые
сцены, ресурсы и файлы зависимостей из `assets/bin/Data` сохраняются из рабочей
базы. Из экспорта обновляются только `Managed/Metadata/global-metadata.dat`,
`ScriptingAssemblies.json` и `RuntimeInitializeOnLoads.json`. Bootstrap редактора —
технический каркас для компиляции: его `level0` и ресурсы не являются игровыми.
Подмена ими данных игры приводит к чёрному экрану и пустым ссылкам SceneHelper.
Инструмент отклоняет базу с такой же стартовой сценой, как у экспорта. Он требует
успешные Unity/IL2CPP-журналы, сверяет build-id `.so`/`.sym.so` и выводит
новый `offsets.ini` и `assets/NxPort/boot-content.json` с контрольными суммами
стартовых данных и метаданных. Упаковщик и проверка NSP сверяют этот манифест.
Регрессионная проверка: `python tools/test_boot_overlay.py`. Параметры: `--base`, `--export`, `--nro`, `--output`,
`--native-elf` (ELF той же сборки NRO), `--unity-log`, `--il2cpp-log`.
Выходной каталог должен быть новым. Инструмент сверяет build-id NRO/ELF
и проверяет все undefined-имена библиотек по собранным таблицам адаптеров
и экспортам загружаемых модулей; неразрешённые имена блокируют staging.
Отдельный аудит: `tools/audit_imports.py --loader <loader.elf> --library <libmain.so>
--library <libunity.so> --library <libil2cpp.so> --report <audit.json>`.

`tools/package_full_nsp.py` принимает `--game` (этот каталог), `--support`
(корень полного комплекта SaltyNX), `--keys` (локальный путь к ключам),
`--hacbrewpack`, `--nacptool`, `--work` (новый рабочий каталог), `--output`
(путь итогового NSP). Нужен Python с `cryptography`. Команды генерации читают
ключи локально; они не копируются в payload и не печатаются в отчёте.

Проверка входит в упаковку: `tools/verify_full_nsp.py` проверяет NCA content IDs,
хеши PFS0/IVFC, код лаунчера, NPDM-политику и каждый файл RomFS. Для повторной
проверки: `python tools/verify_full_nsp.py <file.nsp> --keys <local.keys>`.
Отчёт `.verified.json` относится к конкретному SHA-256 NSP. Это проверка
контейнера и содержимого на ПК; установка/запуск на Switch проверяются отдельно.

Перед упаковкой `stage_full_game.py` объединяет Unity `.splitN` в обычные
файлы и сверяет длину с заголовком сериализованных данных. Это обязательно:
RomFS не допускает объединение файлов на первом запуске. `package_full_nsp.py`
отказывается принимать дерево с не собранными split-файлами.

Если hacBrewPack очень медленно собирает большой RomFS после маленьких файлов,
примените `tools/patch_hacbrewpack_buffer.py` к его `romfs.c` и пересоберите
утилиту. Патч восстанавливает исходный размер рабочего буфера перед каждым
файлом; содержимое payload не меняется и проверяется после сборки NSP.

FSR 3.1 sources are embedded in `unity/Packages/fidelityfx.fsr`. Copy this package
into the Unity project Packages folder. `NxFsr3Build.Export` builds both upscaler
bundles and runs GPU/touch/input checks before Android export. Staging requires
`--fsr-bundles <project>/Build/NxPortAndroid`; stale base bundles are atomically
replaced. Keep the package MIT/AMD notices with binary distributions.

After `patch_nullcheck_site.py`, apply `patch_bounds_site.py <IL2CPP>` before
compile-cpp. It logs invalid array indices and caller offsets, preserving the
original exception. Run `test_bounds_site.py --cxx g++` for its host checks.


### r38: startup collector safety

The r37 console log stalls twice at frames 190/192 while starting the intro,
before the composition upscaler binds. The watchdog cannot acquire the GC
snapshot gate. This identifies an in-progress collection, but the old diagnostic
cannot locate the collector's blocked instruction.

The native bridge now acquires the recursive newlib allocator lock before
suspending mutators and releases it after resuming them, including failure
unwind. Collector log messages use a separate bounded buffer and are merged only
after resume; no collector log or explicit flush waits on a suspended producer.
The watchdog can snapshot only the collector without resuming GC-owned mutators.
Host checks cover the ordering, unwind, snapshot failure, and collector logging
while the ordinary queue is locked. These checks do not prove Switch startup;
r38 remains a console test candidate. No timing, game code or FSR shader change
is included in r38 relative to r37.


### r39: remove shared pthread initialization lock

r38 feedback still stalls before the intro renders. The collector snapshot is
phase 2, PC `svcArbitrateLock`, LR `mutexLock`. Its allocator gate was already
acquired, so the allocator-only fix did not resolve the startup stall.

The Android mutex/condition adapters previously acquired `g_sync_init_lock` on
every operation, including unlock of already initialized objects. If GC paused
that lock's owner, the collector could not use its own unrelated locks. Mutexes,
conditions and rwlocks now resolve backing objects through an address registry:
initialized lookup does not lock or allocate; concurrent first use publishes a
fully initialized candidate with CAS and disposes losing candidates. Another
thread never waits for an initializing owner. Guest storage uses 32-bit accesses
because Bionic objects can be only four-byte aligned. Small registry address
records remain until process exit to permit readers without reclamation locks;
native backing objects are destroyed normally and address reuse shares a record.

The Horizon Boehm callbacks already pause/resume synchronously. They now bypass
POSIX signal ACK posts, waits and retry barriers; normal Android keeps them.
`test_sync_registry.py` exercises a parked allocator/creator, eight competing
publishers, collisions, 50,000 addresses, destruction/reuse, OOM and the actual
mutex/condition/rwlock adapters. `test_gc_sync_ack.py --source <exported
pthread_stop_world.c>` compiles the actual Boehm suspend/restart/barrier code and
checks both backends, including a negative control for the old ACK path.
Collector-only mutex tracing records the pending lock, caller and owner handle.
These are host checks. Switch startup remains a separate acceptance test.


### r40: confirmed futex bucket deadlock

The r39 console log identifies the collector's pending mutex and caller:
`NRO+0x142fc`, resolved with the matching native ELF to `futex_impl` line 194,
the `FUTEX_WAKE` branch. The collector had paused the bucket mutex's owner, so
its wake operation could never acquire the bucket. Earlier synchronization fixes
did not remove this specific bucket dependency.

Raw futex wait/wake now uses Horizon `svcWaitForAddress(WaitIfEqual)` and
`svcSignalToAddress(Signal)`, with no userspace bucket or mutex. The kernel
combines the value comparison and sleep. Both SVCs are already permitted by the
actual launcher NPDM; no launcher policy change is needed. The existing 16 ms
poll bound remains, with capped waits returning a permitted spurious wake. Short
deadline expiry maps to ETIMEDOUT, a changed value to EAGAIN, cancellation to
EINTR, and invalid memory to EFAULT. WAIT uses a relative timeout; WAIT_BITSET
uses an absolute monotonic/realtime deadline. The old bitset broadcast and
approximate wake-count behavior are retained. See the Linux man-pages project
[FUTEX_WAIT](https://man7.org/linux/man-pages/man2/FUTEX_WAIT.2const.html) and
[FUTEX_WAIT_BITSET](https://man7.org/linux/man-pages/man2/FUTEX_WAIT_BITSET.2const.html).

`test_futex_kernel.py` compiles the actual shim with a poisoned userspace mutex.
It exercises wake, atomic value changes before sleep, timeout clocks, overflow,
error mapping and balanced diagnostics, and checks SVC 0x34/0x35 in the binary
NPDM. `--legacy-source <r39 libc_shim.c>` supplies the negative control: the exact
old wake branch must fail on the mutex that the console log identified.
This is a native-only patch; the r39 IL2CPP build and all game/upscaler resources
are unchanged. Host checks pass; Switch startup remains to be confirmed.

## ПК с 8 ГБ ОЗУ

Запускай экспорт Unity, компиляцию IL2CPP и упаковку последовательно.
Для Unity используется `-job-worker-count 1`. После закрытия Unity запускается
инкрементальная компиляция C++ с `--jobs=1` или `--jobs=2`, в зависимости от
свободной памяти. Несколько редакторов Unity одновременно не нужны.
Каталог кэша IL2CPP сохраняется между сборками; при обычных изменениях повторно
компилируются затронутые файлы. Изменение состава generic-типов может потребовать
более широкой пересборки. Число потоков лучше выбирать до её начала.

Упаковщик обрабатывает игровые данные потоком: полный NSP не загружается
в ОЗУ целиком. Поэтапная сборка всё равно даёт один установочный файл.

Для повторных экспортов `tools/reuse_codegen_comments.py` может сохранить
исходники C++, когда изменились только служебные номера Method Definition Index
в комментариях IL2CPP. Проверяется полное совпадение остального кода; файлы
метаданных и регистрации исключены. Инструмент запускается после `patch_all.py`.
Это позволяет сохранить объектный кэш без подмены кода или таблиц метаданных.
