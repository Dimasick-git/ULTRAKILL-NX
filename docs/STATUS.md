# Состояние порта (25.10.2026)

Текущий кандидат — r84 / TEST48 на Unity/GLES. Подготовка языкового обновления:
14 языков с переключателем, таблицы переводов, CJK-бандл и один полный NSP.
Новые переводы 12 языков — машинные черновики с частичной вычиткой;
аппаратная проверка этого пакета впереди. См. [release notes](RELEASE-r84.md)
и [локализацию](LOCALIZATION.md). Коммит и публикация не выполнены.

## Архив: r29, 19.10.2026

**r29 local test на карте/stage. Публичные два ZIP остаются r28.**
Пользователь подтвердил r28: переход Лимбо не завис, но бои около 10 FPS;
попросил оптимизацию и SaltyNX. Захват `uk-build/run28-feedback/debug.log`
15055515 байт, last-boot.log. Реальные интервалы 10.7–15 FPS, CPU UnityMain
70–90% одного ядра. Старый raw scan содержит остаточные адреса: ранняя версия
об ошибке Transform.localScale не доказана. Есть return site GunControl.Update
+0xfb4 на общий блок NullCheck, но точное поле по этому стеку не установить.

r29 добавляет `NxWeaponSlots`: восстанавливает ТОЛЬКО NULL-коллекции, которые
исходный ctor GunControl обязан создать; существующие оружие и ссылки сохранены,
пустой внешний список не наполняется до штатного Start. Editor test реального
типа через FormatterServices (без привязки recovered MonoScript к Editor GO) PASS.
Это ещё НЕ подтверждение устранения NRE: нужны [weapons] repairs и новые логи.

`patch_nullcheck_site.py` после Export и exception trace патчит общий NullCheck:
при NULL пишет точный сгенерированный файл/строку через callback, затем бросает
штатное исключение; non-null путь без callback. Логи [nullcheck] — ограниченные
образцы 1..4,64,1024,8192. Сохранился старый безопасный throw-trace как fallback.

SaltyNX установлен 2.1.1; latest upstream 2.1.3, глобальный модуль НЕ менялся.
Реализован прямой IPC NX-FPS bridge в nx_salty.c/h для NRO, ABI310 сверена с
официальным тегом 2.1.1. Named port InjectServ, cmd7 handle, cmd6 reserve;
только успешные EGL presents. Реальные avg FPS, ticks[10], frame counter/API2.
Программный cap 15..60 из FPSlocked / FPSlockedDocked. Сервис опционален,
активный чужой producer не перезаписывается. Config saltynx 0 отключает init,
default 1, init ПОСЛЕ read_config. IPC/Status Monitor на железе ещё не подтверждены.
Нет ReverseNX/ZeroSync/NVN counters/игровых patches/загрузки saved FPSLocker dat.
Host test настоящего telemetry/pacing кода PASS (60 FPS без off-by-one, режимы).

На карте physics_hz 60 (штатно 0, project timestep .008/125Hz), FSR 480p,
saltynx 1. SetFixedDeltaTime через настоящий Unity icall один раз; wall clock/
timeScale не тронуты, скриптовые смены шага не подавляются. Gore снижен 256->64
только в карточном LocalPrefs с сохранением прочих ключей и backup. В core ZIP
Preferences нет. Нужны тесты прыжка/рывка/столкновений/оружия/slowmo/роликов.
Откат профиля: physics_hz 0, render_height 540, вернуть LocalPrefs из backup.

Unity `unity-r29-final.log`: weapons schema/invariants, Input14+triggers/UI A/B,
GPU EASU/RCAS PASS, Android Succeeded errors0 (transient CS0246 первого Bee прохода
исчез при повторном). IL2CPP `il2cpp-r29.log`: 493 successful, 0 failed, 6m36s.
Build-id 6c3d82e6c9b7e5441be9d899a0b54f02a0605fc7. Все offsets заново, 17 video hooks.
Обе metadata SHA 01a5d2efac650abe36427d964dce3e8ecd26c49d6e7d9670172cb3ec12883234.
Lib SHA 8b2890dd457fc680cf16a0e5e4175a01da1a92667edc4351df1e5ecd13fb3a7f.
Итоговый NRO SHA 43c47b482648371cab87ba0e01839288c227393c74253b9b41723a05847a0bfd.

Архив `release/ULTRAKILL_NX_r29_test.zip` 31859029 байт, SHA
12700febcb7895fce2f6d5c3d6914c14516a694982c18c3ba43d28ce157bdb51.
`uk-build/r29-verified.json` — все хеши. Финально сверены КАЖДЫЙ core в ZIP,
card и stage. Резервная r28 `backup-run28/{card,stage,build}`, включая old lib/sym.
SHA старого FULL/UPDATE r28 не менялись. Не подменять ими тест r29 по имени!

Source main 2080afed471f7d5a8792dc3cf48e494aa07645b4, CI39172925259 SUCCESS,
6 host checks. Публичные docs SALTYNX/PERFORMANCE-r29, README/BUILD/CREDITS/CHANGELOG
обновлены; явно отмечен local test. Release r1 остаётся обычным, ровно два ZIP r28,
его notes обновлены: переход без зависания подтверждён, тяжёлые бои 10–15 FPS.
Пользователю задан вопрос о повторе ТОГО ЖЕ боя r29, FPS в оверлее и движении;
ответ ещё ожидается. Следующий шаг — читать НОВЫЙ r29 debug.log и [saltynx],
[weapons], [nullcheck] source:line, [perf-profile] physics и [perf]/[cpu-perf].

Ниже — история r28 и более ранних сборок.

**r28 записана на карту и stage, проверены все core SHA-256.**
Пользователь r27: баги, вылеты и провисания в Лимбо; дальше пока не прошёл.
Захват `uk-build/run27-crashes`: debug.log 14636635 байт и свежий отчёт
01792356999_01005ce3b1a40000.log. Data Abort NULL в Umbra::Tome::getStatus,
PC 0x35806b7424, правильная base libunity 0x357f62a000 => +0x108d424;
LR +0xd45274 в UmbraModule::LoadUmbraTome. TomeLoader возвращал NULL.

`nx_umbra.c` повторяет валидный путь LoadUmbraTome с проверкой результата
до getStatus. Адреса из совпадающих Unity symbols; проверяются сигнатуры
и BL-цели init/status до hook. Host test guard PASS. Ввод данных и статус
корректных Tome сохранены; почему именно загрузчик Tome отказал, ещё неизвестно.

`patch_exception_trace.py` добавляет callback до unwinding NRE; native
`nxb_exception_trace` снимает до 7 ограниченных образцов return-address candidates,
не останавливает потоки и не использует общие watchdog-буферы. Проверяет границы
стека, RX LOAD и реальные читаемые/исполняемые страницы перед чтением call site.
Это диагностика, не исправление причины постоянных NRE.

IL2CPP r28: 9 successful nodes, 0 failed, build-id
954c858941ebba920f0de05baf9c0dbb461189e1; 28 offsets заново, 17 video hooks.
Metadata прежняя r26. Добавлены три unity.umbra_* offsets (83 записи из лимита 96).
NRO SHA d83da647908f0462294b02f7558ad9e4ec67d2bc56f1a3e8fc34431274dde533.
Lib SHA 49293dfca0c1bb016aa9b140601b8cf573703505c8ec6f389705ffc3b5bcb28b.
Резервная r27: uk-build/backup-run27/{card,stage}; r26 аналогично backup-run26.

Исходники опубликованы main ccab909 + b2fc7e7401af09f221bcee00385a0197c089b347.
CI Host checks 39169228359 — success, 5 проверок (CPU/dladdr/GC/log repeat/Umbra).
README, BUILD, FSR, GC, TESTING, ARCHITECTURE, CONTRIBUTING обновлены.
README/CHANGELOG честно отмечают, что стабильных 60 FPS и фикса всех NRE нет.
Исторические claims r23/r24 о включённом GC исправлены.

Локально ULTRAKILL-NX_UPDATE.zip и ULTRAKILL_NX_r28_update.zip: 30545922 байт,
SHA a9f522b44cae16dbe47282cc353910d603d785fb899386c633e098f1de190d6b.
Это полный core update от самой первой r1, с новым стандартным config.
Полный единый LOCAL ZIP: uk-build/release/ULTRAKILL-NX_FULL.zip, 3360636343 байт,
SHA d089c5bcbe7b6a788072b45dab7621006b72b6f641694aa8831f1cb650ac7f33.
Проверен CRC и каждый файл; private logs/keys/saves не включены.

Пользователь вновь разрешил публикацию и просит ТОЛЬКО два новых ZIP с именами
ULTRAKILL-NX_PART_1.zip и ULTRAKILL-NX_PART_2.zip, обычный release r1.
Repackage full release 39169438548 запущен после проверки серверного SHA update.
Workflow 39169438548 завершён SUCCESS. Проверены каждый файл и архивные SHA;
в обычном r1 остались ровно два PART ZIP, старые assets удалены.
PART_1: 1680388200 байт, SHA 3d4eb965b403bdf4da14501187fa827cbff93f69ae064fb506e02abfb4cb363a.
PART_2: 1704057333 байт, SHA 9145e463785e861c2346e9184706699a7e414eecfc8f086614576b61bb0bb055.
Tag r1 указывает на b2fc7e7401af09f221bcee00385a0197c089b347.
Отчёт: release/r28-packaging-report.json; полный runner log r28-packaging.log.
Локальные FULL и UPDATE проверены независимо (CRC + каждый файл + SHA архивов).
Повторные скачивания канонических PART на этот ПК остановлены как избыточные:
скорость около 1 МБ/с суммарно; неполные файлы перенесены в
release/incomplete-downloads-r28/*.partial, НЕ считать их установочными ZIP.
На ПК пользователь получает готовые FULL.zip и UPDATE.zip, оба целые/проверенные.
Репозиторий остаётся private. Вопрос пользователю о повторе 1-1 -> 1-2 с r28
ожидает ответа; карточный прогон r28 ещё не получен.

Ниже — история предыдущих сборок.

**Локальная r26 на карте, GitHub пока остаётся r24 по просьбе пользователя.**
FSR1 EASU+RCAS (официальные AMD headers, fragment FP32), отдельный Android
shader bundle; GPU-проверка OpenGLES3 на ПК пройдена. Композиция включает HUD.
На E: включены fsr=1, render_height=540, fsr_sharpness=0.5; весь комплект
с библиотекой, offsets, двумя metadata, NRO, бандлом и лицензиями сверён по SHA256.
Подробности и A/B: PERFORMANCE-r26.md. Реального профиля FPS/CPU r26 ещё нет.
В лог нужны [fsr] active EASU+RCAS, first output frame и [cpu-perf].

Исправлены sched_getaffinity/setaffinity и raw syscall 122/123, фактическая
маска/количество CPU берутся из Horizon. dladdr сообщает имя и base библиотеки.
На ошибках shader link сохраняются первые восемь пар GLSL. Убраны покадровые
строковые записи PlayerPrefs. cosh/sinh/tanh связаны с настоящим libm.
Host-тесты CPU adapter, dladdr и GC bridge — PASS; Unity export errors=0,
нативная сборка: 350 nodes, 0 failed; 28 offsets и 17 video hooks выведены заново.
IL2CPP build-id: 82b4a02b240a2fd75773b5977477a85fbd59f208.
Патч: uk-build/release/ULTRAKILL_NX_r26_test.zip, 30542927 байт,
SHA256 9486ed039e429aef6afb0a05f1b58c49153b31b1281cbc7147ac2b07a6bcd360.
Резервные r25 core-файлы и config: uk-build/backup-run25/{card,stage}.

**r25 исправила ошибку r23/r24 в режиме GC:** enum ENABLED=1, а не 0.
Добавлена проверка il2cpp_gc_is_disabled после запроса. Аппаратный лог
uk-build/run25-ram-stable/debug.log: disabled=0, stw_passes=35/36,
managed heap около 80 МБ. r23/r24 фактически оставляли GC выключенным!

Свежий вылет r24 сохранён в uk-build/crash-20261018-latest: Undefined Instruction
Door.Update, expected 0x6d4423e9 vs actual 0x7f4423e9. Пользователь подтвердил
перекрученные тайминги RAM и исчезновение вылета после их возврата; не считать
сбой доказанным дефектом Door/GC. Длительный прогон всё ещё нужен.

**r24: физическая A подтверждает, B отменяет в UI.** NxSwitchGamepad наследует
Gamepad и переопределяет usages лицевых кнопок; позиционные игровые действия
сохранены. NxInputCheck проверяет реальный UI asset игры: A/B отдельно,
совместное нажатие, отпускание, названия кнопок и изоляцию курков — PASS.
Unity export: Succeeded, errors 0; native compile: 82 nodes, 0 failed.
IL2CPP build-id 750c1b315884b6ba2d86de14d7b36eb5631f5a70; 28 offsets проверены,
GC bridge r23 сохранён. Metadata изменилась, для отката нужны ОБЕ старые копии.
Карта E: и out/switch/ultrakill обновлены согласованным комплектом, хеши совпали.
Резервная r23: uk-build/backup-run23. Патч: ULTRAKILL_NX_r24_update.zip,
30516489 байт, SHA256 02b0fbd9695a1f8d9e4cb861e145a6a36076eba71e39ec0abbfdf3edea0badb0.
FSR не реализован. План подключения в docs/FSR.md. Аппаратная проверка r24 нужна.

**r23 на карте для проверки issue #1: настоящий Horizon GC bridge.** Лог друга
показывает отказ коммита 4 МБ при live 2466 МБ после длительной игры и загрузки
Level 0-3. В r22 GC был отключён. r23 патчит экспортированный Boehm через
tools/patch_gc_horizon.py: пауза потоков, SP и полный GP/NEON-контекст как корни, безопасное
возобновление и общий gate со снимками watchdog. Автоматический GC возвращён;
host-тесты и обе нативные сборки проходят, аппаратная проверка ещё нужна.
IL2CPP build-id 1b6e0d97eda7192658d49d2ad1a382e520d2b031; все offsets перевыведены,
метаданные прежние. Обновление ULTRAKILL_NX_r23_update.zip — 29231355 байт,
SHA256 d856f54ad5fb7e37d4bcb0ef857e1036f2cd372659bce5a74633f0c7a6550031.
Полные ZIP r1 также обновлены до r23, проверены в GitHub Actions 39148018929.
Отчёт и хеши опубликованных ZIP: uk-build/release/r23-packaging-report.json.
Карта E: повторно проверена по всем шести хешам r23. Откат: uk-build/backup-run22.
Публичное описание: [GC-r23.md](GC-r23.md).

**r22 записана на карту: исправлен вылет в диагностике на второй миссии.** Отчёт Atmosphere
указывает на чтение `v-4` в нашем сборщике стеков: адрес начала модуля приводил
к обращению на недоступную предыдущую страницу. Убраны периодические снимки
при работающих кадрах, доступы проверяются по исполняемым сегментам и разрешениям
ядра. Тест настоящего обходчика с PAGE_NOACCESS — PASS. См. [RUN22.md](RUN22.md).

**r21 записана на карту: исправлена передача курков.** ZL/ZR ошибочно вызывали также DpadUp/Down
через WithButton с номерами 32/33. Курки теперь используют только свои оси.
Проверка в настоящей Unity Input System: 14 кнопок, курки, сочетания и отпускание —
PASS. См. [RUN21.md](RUN21.md). Пользователь подтвердил запуск r20, журнал
подтверждает первый кадр и завершение заставки на консоли.

**r20 записана на карту: реализован программный потоковый видеоплеер.** Падение r19 связано с
отсутствующим Android импортом AMediaExtractor_new при открытии заставки.
Добавлены JPEG декодирование в отдельном потоке, PCM аудио, managed интеграция
VideoPlayer и четыре перекодированных ролика. Пересобраны IL2CPP и метаданные,
обновлены все смещения. Подробности и проверки: [RUN20.md](RUN20.md).
Аппаратная проверка видео и производительности ещё нужна. Ниже — история запусков.

**NRO r19 записан на карту для проверки производительности.** Удалены старые
подмены Time.*, включён профиль с внутренним рендером 540p, меньшими текстурами
и gore, ограниченным кэшем шейдеров 16 блоков и целью 60 FPS. Добавлен [perf]
с реальными кадрами представления и p95. Подробности: [RUN19.md](RUN19.md).
Результат на Switch ещё не проверен; стабильные 60 FPS не подтверждены.
GitHub-релиз r1 пока содержит предыдущую r18.

**NRO r18 записан на карту.** Пользователь подтвердил запуск игры с r17;
обе оси гироскопа оказались обратными. r18 и config.txt разворачивают обе оси,
сохраняя чувствительность. См. [RUN18.md](RUN18.md).

**Пакет r17: форвардер r16 + NRO r17.** Аппаратный запуск r16 подтвердил
работу demand-memory, затем упёрся в физическую память при распаковке шейдеров:
2453 МБ выделено, около 2 ГБ в ALLOC_CACHEOBJECTS. r17 включает штатный
`Shader.maximumChunksOverride=4` до загрузки любых шейдеров. NRO и offsets.ini
записаны на карту, проверены хеши. NSP переустанавливать не нужно.
Изображение/звук после исправления ещё требуют аппаратной проверки.
См. [RUN17.md](RUN17.md). Ниже — история предыдущих запусков.

**Пакет r16: форвардер r16 + NRO r15.** Форвардер r15 не создавал процесс:
системный отчёт подтвердил 2001-0104, Applet-пул не вмещал 32-МБ служебный резерв.
r16 исправляет тип на Application и согласует ID ACI/ACID. Готовый NSP извлечён,
его NPDM проверен; аппаратный запуск теперь подтверждён. См. [RUN16.md](RUN16.md).

Запуск r14 завершился подтверждённым OUT OF MEMORY
при загрузке шейдеров. r15 переводит mmap на выделение физических страниц
по требованию; NRO и обновлённый NSP уже записаны на карту с проверкой SHA-256.
Нужно установить новый форвардер через DBI и запускать ULTRAKILL NX с HOME.
Аппаратный запуск r15 пока не проверен. Подробности и тесты: [RUN15.md](RUN15.md).
История ниже описывает более ранние сборки.

## Доказано на ПК
Настоящий `ULTRAKILL.exe` (плеер 2022.3.29f1) работает на **OpenGL ES 3.2** только на наших
переведённых шейдерах (`uk-build/ukgame-gles`). Он грузит экран загрузки и интерфейс, маски
и TextMeshPro работают, сцены обучения грузятся из переделанных бандлов. Шаги:

1. `tools/port_all.py <aa игры> <выход> [--target android|keep]` — все бандлы
   Addressables, шейдеры GLCore→GLES3 (58 из 81 с шейдерами, около 10 мин).
2. `tools/port_files.py` — шейдеры на месте в основных данных и встроенных ресурсах:
   `resources.assets`, `sharedassets0.assets`, `Resources/unity_builtin_extra` (42),
   `Resources/unity default resources` (6). Запись через `sfpatch.py`: UnityPy портит
   файлы плеера при сохранении.
3. `tools/patch_apis.py globalgamemanagers` — `BuildSettings.m_GraphicsAPIs` → `[11]` (GLES3).
   Без этого «-force-gles32 ... was not built from editor».
4. Стенд: копия игры, в `StreamingAssets/aa` — переход (junction) на переделанное дерево;
   запуск без флагов. Игра рисует только с фокусом окна.

## Главные выводы
- Основные данные плеер принимает только своей версии: «Invalid serialized file version».
  Поэтому Android-часть собирается на **Unity 2022.3.29f1** (версия игры). Данные берутся
  оригинальные, из игры, а редактор даёт только движок и IL2CPP-код.
- Редактор не загружает DLL-плагины с зарезервированными именами (`Assembly-CSharp`,
  `UnityEngine.UI`, `Unity.TextMeshPro`). Поэтому сцены, собранные редактором, «битые»,
  и данные редактора не используем. Переименовывать сборки нельзя: бандлы ссылаются на
  `Assembly-CSharp` по имени.
- Адреса libunity у каждой версии свои. `tools/unity_syms.py` выводит их из
  `libunity.sym.so`. Natives загрузчик ловит из `RegisterNatives`. Заплатки идут через
  `offsets.ini`: `patch.unity.* = адрес было стало`, `poke8.unity.* = адрес значение`.
  Для 62f2 найдено:
  - время — `AndroidVSync::WaitForLastPresentationAndGetTimestamp`, `tbnz` → `b` на ветку
    `GetTimeSinceStartup`;
  - звук FMOD — `AudioManager::InitNormal`, `mov w1,w21` → `mov w1,#22`;
  - буфер OpenSL — в `OutputOpenSL::init` второй `b.ls` → `b`;
  - Swappy — `Swappy::s_ForceDisable = 1`.

## Где что лежит
- `%USERPROFILE%\ultrakill-nx` -- этот репозиторий: загрузчик (`loader/`), инструменты
  (`tools/`), C#-слой порта (`unity/Assets/NxPort*`), форвардер для DBI (`forwarder/`).
  Git локальный, коммитов нет.
- `%USERPROFILE%\uk-build` -- рабочая папка сборки: проект Unity `ukport29` (29f1),
  экспорт `ukport29/Build/AndroidExport`, данные `android-data`, бандлы `aa-android`,
  готовая папка для карты `out/switch/ultrakill`, `unity29.ini`, журналы запусков
  `debug-run*.log`.
- `%USERPROFILE%\uk-tools` -- сторонние программы (AssetRipper и т. п.).
- Карта: `E:\switch\ultrakill` (игра), `E:\ULTRAKILL_NX_forwarder.nsp` (ставить через DBI).

## Сборка для Свитча
1. `ukport29`, Unity 2022.3.29f1: `NxBuild.AndroidExport`, затем
   `il2cpp.exe --compile-cpp ... --jobs=3` (аргументы из `unityLibrary/build.gradle`).
2. `prep_data.py` -- основные данные игры для Android; `port_all.py --target android`.
3. `unity_syms.py` -> `unity29.ini`: заплатки libunity по символам (время, FMOD, буфер
   OpenSL, FrameTimeTracker, streamingAssets без `jar:`, 21 место регионов 64 МБ) и
   `unity.app_path_buf`.
4. `pack_switch.py` -> `out/switch/ultrakill`. Внутри `fix_catalog.py` и `derive_offsets.py`
   (GC, JavaVM, Time.*, `Logger.IsLogTypeAllowed` по символам `libil2cpp.sym.so`).
5. Загрузчик: `cd loader && make clean && make` (msys64 + devkitA64), `.nro` -- на карту.

## Что пройдено на консоли (18.10)
Движок и il2cpp поднимаются, звук FMOD/OpenSL запускается, Addressables читают каталог
и грузят бандлы обучения (~1 ГБ). Чинилось по пути, всё в загрузчике:
- путь «APK» `/switch/ultrakill/base.apk` в буфер `FileSystem::SetApplicationPath`
  (иначе `dataPath` пустой); `/base.apk/` и `jar:file:` сводятся к папке игры;
- `opendir`/`unlink`/`rename`/`mkdir` через то же перенаправление (копия метаданных il2cpp);
- страницы TLS потоков заранее (`nx_prealloc_tls`): иначе ядро клало их в окно OC
  и коммит падал с 0xd401;
- inode из пути в `stat`/`fstat`: fsdev отдаёт 0, il2cpp видел «Sharing violation»;
- `clock_gettime`: перевод номеров часов bionic -> newlib. Unity берёт `CLOCK_BOOTTIME`
  (7), и часы движка стояли на нуле: игра висела в сцене Bootstrap с чёрным экраном.
  **Эта правка на карте, на консоли ещё не проверена.**

Строка `[diag]` в `debug.log` раз в 300 кадров показывает часы движка, timeScale и
активную сцену -- первое, что смотреть при чёрном экране.

## Открытые вопросы
- GC il2cpp: мост сигналов -- только подтверждения, остановки потоков нет.
- MaskedOcclusionCulling -- заглушка.
- Перепроецирование кадра (`reproject`) на консоли ещё не проверялось.
- Шрифт для японского, корейского и китайского в комплект пока не входит: без него
  эти языки показывают пустые квадраты.
