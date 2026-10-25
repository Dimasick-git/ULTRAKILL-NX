# Проверка производительности перед 2.0.0

TEST1 / r37 — сборка для повторного аппаратного прогона. Релиз 2.0.0 ещё
не опубликован. Проверки на ПК не подтверждают стабильные 60 FPS.

## Исходные данные

В рабочем запуске r34 от 19 октября 2026 года есть пятисекундные окна
с 22,5–30 FPS, p95 до 70,7 мс и отдельный кадр загрузки 10 771 мс.
В конце того же запуска встречаются 60 FPS: последний участок не описывает
нагрузку всего уровня. Счётчик управляемых исключений в этом запуске равен нулю.

`swap_wait` около 0,3 мс показывает только время `eglSwapBuffers`. Он не
исключает ожидание GPU внутри других вызовов и сам по себе не доказывает,
что вся просадка вызвана CPU. Нужны сопоставимые замеры одного боя.

## Изменения TEST1

- Обычный лог поступает в ограниченный буфер 512 КиБ. Отдельный native-поток
  записывает его на SD; обычный вызов логирования больше не вызывает запись
  или flush из UnityMain. Аварийные сообщения сбрасываются принудительно.
  Переполнение учитывается в журнале, критическое сообщение повторяется
  после сброса очереди. Ошибка запуска потока сохраняет синхронный режим.
- Гироприцел использует типизированные поля CameraController вместо reflection
  и упаковки float/bool в объекты каждый кадр. Закрытое меню настроек кэшируется,
  включая его неактивное состояние.
- Ранний запуск заполняет отсутствующий или пустой `Preferences/LocalPrefs.json`
  прежними значениями SD-комплекта: `vSync=false`, `frameRateLimit=3`,
  `maxGore=64`. Это выполняется только при `performance_mode=1`.
  Непустой файл пользователя не заменяется. В исследованном установленном
  комплекте этот файл имел нулевую длину, поэтому прежние значения отсутствовали.
- 73 ранее неразрешённых имени получили записи в таблице адаптеров.
  Математические, строковые и файловые операции реализованы обычными адаптерами.
  Недоступные Android MediaCodec/ImageReader возвращают ошибки NDK и пустые
  выходные параметры; наличие аппаратного декодера не имитируется.
  Видео обслуживает существующий NX-плеер. Аудит трёх реальных ELF проверяет
  11 + 422 + 296 импортируемых имён; неразрешённых имён не осталось.
- Восемь сгенерированных методов имеют ограниченные замеры входа/выхода:
  автоаим, камера, движение, оружие, выбор цели врага и постобработка.
  Вызовы агрегируются в памяти; строка `[cpu-scope]` выводится раз в пять секунд.
  Среднее и максимум — включительное прошедшее время, включая ожидания внутри
  метода. Вложенные замеры нельзя складывать как независимые доли CPU.
  Первый вызов может включать однократное разрешение адреса callback.

Переходы времени игры и исходный `fixedDeltaTime=0.008` сохранены.
Настройки разгона и таймингов памяти сборка не меняет.

## Прогон

Установить полный TEST1 NSP поверх предыдущего пакета через DBI. Сохранения
остаются на SD. Проверить обычный тап и подтверждение экрана первого запуска,
переход между уровнями, затем повторить тот же тяжёлый бой с теми же
настройками автоаима, разрешения и часов. Сравнивать после прогрева ресурсов.

В журнале проверить `[defaults]`, отсутствие `unresolved import`,
`[perf]`, `[cpu-perf]`, `[cpu-scope]`, `[gc]` и `[diag] exceptions`.
При вылете сохранить соответствующий отчёт Atmosphere вместе со свежим
`debug.log`. Отдельный вылет на 0-2 остаётся открытым до повторной проверки;
несколько старых crash reports не считаются одной доказанной причиной.

Полный NSP проверяется после упаковки: хеши NCA, PFS0/IVFC, политика NPDM,
происхождение стартовых данных и каждый файл payload. Отчёт `.verified.json`
лежит рядом с установщиком. Проверки адаптеров входят в Host checks.


## Render recovery and FSR 3.1 (local r37 test)

The user reports world rendering disappearing after a mission transition while
crosshair rendering continues. Changing render scale restores the image.
The port now rebuilds the game MRT/depth/composition chain after additive scene
loads/unloads and camera replacement. It detects released GPU allocations even
when the RenderTexture object remains alive, matches SetupRTs integer dimensions,
and restores same-size camera target-buffer bindings. Recovery is not conditional
on the user changing resolution. The symptom has not yet been retested on Switch.

Terminal ShopButton subscribes to ControllerPointer.OnPressed rather than a
standard uGUI click. Touch release now dispatches that event for the original
pressed terminal target, alongside the ordinary click, with cancellation/drag
rules retained. The recovered editor cannot instantiate the original internal
MonoBehaviour; its real event ABI is tested using an uninitialized instance.
End-to-end terminal routing requires a console test.

FSR3Unity revision 71879e71aa72465a79bdf009d7d71f9f7ab83066 supplies genuine FSR 3.1
reconstruction shaders and a Unity compute backend. The local fork adapts storage
formats and SPD reads for GLES. See unity/Packages/fidelityfx.fsr/NXPORT.md.
The SWITCH panel offers OFF / FSR 1 / FSR 3.1 TEST. Temporal upscaling remains
opt-in; no frame generation is implemented. Real PC OpenGLES3 GPU tests pass for
multi-pass dispatch, history reset, continuation and finite output. Android GLES
shader compilation no longer reports unsupported storage formats. Warnings about
minimum GL storage-buffer limits remain; target hardware still needs testing.
A composition-difference reactive mask limits HUD/post-effect history pollution;
this requires visual testing with the game's effects and moving enemies.
FSR 3.1 may cost more GPU time than FSR 1. It cannot fix a CPU bottleneck by itself.

The captured log at r37-render-feedback/debug.log contains older sessions and a
final r36 session. Two IndexOutOfRangeException messages have no managed stack;
they do not identify their originating method. The new exception-site and timing
instrumentation should locate recurring errors. Do not describe these as fixed.

No source commits, pushes, release uploads or SD game writes are authorized at
this stage. The candidate is a local test NSP for manual DBI/MTP installation.

The final FSR test also rejects GPU/driver error logs; earlier color-only probes
missed a failed luma kernel. Luma UAV slots were compacted and the SPD buffer
received an explicit register. The strengthened 191x191 PC GLES test passes
without GLSL compilation errors. Moving-object/skinned motion-vector coverage
and visual quality with game post effects still require console testing.

Final shader input checks caught a missing ShaderLab _MainTex property: Blit
was sampling the grey placeholder instead of the supplied texture. Explicit
_MainTex/_World properties and a full-precision depth sampler are now included.
GPU tests validate near-far float depth values, Gamma/linear roundtrip and both
reactive-mask cases, in addition to the FSR context. The shipped bundle is the
one from unity-fsr3-input-final.log; the ARM64 code is from final7.
