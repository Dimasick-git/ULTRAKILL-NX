# r20: потоковое видео на Switch

## Причина падения r19

Отчёт `01792311324_01005ce3b1a40000.log` содержит Instruction Abort на
`0x17be100`. LR соответствует `AndroidVideoMedia<AndroidMediaNDK::Traits>::CreateExtractor`
в нашей libunity; предыдущая инструкция вызывает `AMediaExtractor_new@plt`.
Этот импорт отсутствовал, и загрузчик оставлял в PLT адрес релокации как
диагностическое значение. При попытке открыть заставку процесс переходил по нему.
Это установленная причина данного падения; она не доказывает отсутствие других ошибок.

## Реализация

- `tools/prepare_video.py` преобразует WebM в NXVID001: индексированные JPEG,
  30 кадров/с, ширина до 640 пикселей, потоковый float32 PCM 48 кГц stereo.
- `loader/source/nx_video.c` декодирует JPEG через libjpeg-turbo 2.1.2 в отдельном
  потоке. На игрок выделяются два RGB буфера декодирования и один буфер выдачи;
  весь ролик в RAM не читается. Устаревшие запросы кадров пропускаются.
- `unity/Assets/NxPort/NxVideoPlayer.cs` находит VideoPlayer, отключает Android
  компонент и выводит программно декодированную текстуру. Поддержаны RenderTexture,
  MaterialOverride и экранный вывод CameraFar/Near. Звук идёт через AudioSource.
- 17 обёрток managed VideoPlayer перенаправлены на новый плеер: состояние,
  время, текстура, размеры, Play/Pause/Stop/Prepare и перемотка. Это сохраняет
  переходы и пропуск заставки в исходном коде игры.
- Ранний вызов Android `AMediaExtractor_new` безопасно возвращает NULL, пока
  managed плеер ещё не подключился. Реальный вывод видео обеспечивает новый backend.

Подготовлены четыре ролика: `splashscreen`, `5-S end video new`,
`fuckin_finland`, `uk_museum_ingame`. Суммарно 12396 JPEG кадров, около 292 МиБ.
У Finland в исходнике нет аудиодорожки. Исходные WebM сохранены.
Маленький 5-S ролик остаётся 164×92; остальные подготовлены 640×360.

## Сборка и размещение

Unity 2022.3.29f1 экспортирован с обновлёнными NxPortBootstrap, NxVideoPlayer и
link.xml, затем выполнен нативный `il2cpp.exe --compile-cpp` из экспорта.
Экспорт: `%USERPROFILE%\uk-build\ukport29\Build\AndroidExport`.
Логи: `build-video20.log`, `il2cpp-video20.log` в `%USERPROFILE%\uk-build`.
Новый libil2cpp и sym имеют одинаковый build-id
`2c2de482e5de4155c223b7b7bfaaf6f65993df49`.

Все 28 смещений il2cpp заново выведены `derive_offsets.py`, включая 17 video
hooks. libunity и managed resource-файлы не изменились (проверены SHA-256),
их существующие заплатки сохранены. Общее число ключей укладывается в 96.

В staging и на карту записаны вместе, SHA-256 сверены:

- `ultrakill_nx.nro`, `libil2cpp.so`, `offsets.ini`;
- `assets/bin/Data/Managed/Metadata/global-metadata.dat`;
- `assets/VideosNX/*.nxv`;
- уведомление и лицензия libjpeg-turbo в `licenses/`.

На карте также обновлён **кэш** `il2cpp/Metadata/global-metadata.dat`:
оставлять старые метаданные с новой библиотекой нельзя. Сохранения и Preferences
не перезаписываются. Действующий HOME NSP менять не требуется.
Манифест SHA-256: `r20-sha256.json` рядом с NRO. SHA-256 загрузчика:
`79daae1e5dbc5ffbdc3dbf8962c9941a12b2ad4d13956b2da56d23f72bb2d957`.
Предыдущие файлы сохранены в
`%USERPROFILE%\uk-build\backup-run19\pre-video20`.

Для повторной подготовки: `python tools/prepare_video.py input.webm output.nxv`;
нужны ffmpeg/ffprobe в PATH. Встроенные WebM сначала извлекаются из ресурсов
videos.bundle по m_ExternalResources offset/size. Готовую VideosNX нужно включать
в пакет вручную: обычный pack_switch.py её пока не создаёт.

## Проверки и ограничения

- Unity export: Succeeded, 0 ошибок; IL2CPP: 404 successful nodes, 0 failed.
- devkitA64: загрузчик с libturbojpeg собран успешно.
- Host-тест компилирует настоящий nx_video.c с Windows адаптером потоков и
  настоящим TurboJPEG: декодирование кадров, неизменность выданного RGB во время
  работы декодера, ненулевой PCM, пауза/продолжение/перемотка, getters/commands,
  GC texture handle, устаревшие handles, отсутствующие файлы — PASS.
- Проверены заголовки, индексы, границы всех 12396 JPEG и размеры PCM.
- Реальный кадр заставки просмотрен: BIOS/BLOOD экран декодируется правильно.

Воспроизведение на Switch ещё требует проверки пользователем: заставка с аудио,
пропуск, переход в меню, игровые видеоповерхности. CameraFar/Near выводится через
IMGUI, его порядок относительно интерфейса требует аппаратной проверки.
Исходное видео 30 FPS; это независимо от цели 60 FPS в игровом рендере.
Профиль производительности r19 сохранён, стабильные 60 FPS на уровнях не доказаны.
Публично доступный пользователю приватный GitHub релиз r1 пока содержит r18.
