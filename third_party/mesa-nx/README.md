# Mesa для Switch с кэшем шейдеров

`lib/` — `libEGL.a`, `libGLESv2.a`, `libglapi.a`: Mesa 20.1.0-rc3 из пакета devkitPro
`switch-mesa 20.1.0-5` с теми же опциями, но с `-Dshader-cache=true`. Загрузчик берёт их
раньше `portlibs` (`MESA_NX` в `loader/Makefile`); системный `switch-mesa` не меняется.
`libdrm_nouveau.a` остаётся из `portlibs`. Сами `.a` в git не идут (`.gitignore`).

## Зачем

У сборки devkitPro дисковый кэш выключен для `horizon`. Без него `ctx->Cache == NULL`,
`GL_NUM_PROGRAM_BINARY_FORMATS = 0`, и Unity не сохраняет собранные программы: каждый
запуск заново компилирует и линкует шейдеры (в прогонах до 280 мс на программу).

С кэшем объект `disk_cache` есть всегда, `st_init_extensions` ставит
`NumProgramBinaryFormats = 1`, и Unity сама пишет бинарники программ в
`<корень игры>/UnityShaderCache`.

В бинарнике программы только TGSI. При `glProgramBinary` nouveau заново собирает из него
машинный код: в прогоне r68 это 13 мс в среднем и до 30 мс на программу. Начиная с r69
этот код кладётся в файловый кэш Mesa `<корень>/mesa_shader_cache`. Файловый кэш работает,
только если задан каталог (`MESA_SHADER_CACHE_DIR` или `MESA_GLSL_CACHE_DIR`).
GLSL-уровень этого кэша (пропуск компиляции, метаданные программ) на Switch включается
отдельно (`MESA_SHADER_CACHE_GLSL=1`): он дублирует бинарники Unity.

Ключ `gl_program_cache` в `config.txt`:

| значение | env, который ставит загрузчик | что работает |
|---|---|---|
| 0 | `MESA_SHADER_CACHE_DISABLE=1` | ничего, как до r68 |
| 1 (по умолчанию) | `MESA_SHADER_CACHE_DIR=<корень>`, `MESA_GLSL_CACHE_MAX_SIZE=256M` | бинарники Unity + машинный код nvc0 |
| 2 | то же + `MESA_SHADER_CACHE_GLSL=1` | плюс GLSL-уровень кэша Mesa (опыт) |

В журнале: `[shader-binary]` — загрузки и сохранения бинарников Unity, `[shader-code]` — кэш
кода nvc0 (hit = код с SD, miss = кодоген и запись).

## Что изменено в Mesa (`switch-mesa-shader-cache.patch`, поверх патчей devkitPro)

- `meson.build`: `shader-cache` разрешён для `horizon`.
- `src/util/disk_cache.c`, Horizon:
  - нет проверки uid, `$HOME` и passwd; каталог только из env;
  - индекс в памяти, 16k слотов (320 КиБ) вместо mmap 1.3 МиБ;
  - изменённые слоты дописываются в `index` через stdio из потока кэша;
  - нет flock (процесс один), вместо `fstatat` — `stat`, размер файла — `st_size`;
  - `.tmp` закрывается до `rename`/`unlink` (fs Horizon не трогает открытые файлы);
  - один поток очереди, zlib уровня 1.
- `src/util/disk_cache.h`: `disk_cache_get_function_identifier` на Switch хеширует id
  сборки (`disk_cache_switch_build_id()`: версия + время компиляции `disk_cache.c`).
  Program binary от другой сборки Mesa не проходит проверку sha1 драйвера, и Unity
  собирает программу заново.
- `disk_cache_stores_glsl()`: пока GLSL-уровень выключен, Mesa на линковке не ищет
  метаданные на SD, не сериализует их и TGSI. TGSI сериализует только `glGetProgramBinary`,
  когда его вызывает Unity.
- `nvc0_program.c`: кэш результата `nvc0_program_translate`:
  - ключ: тип стадии, чип, `num_ucps` и TGSI;
  - хранится: код, relocs, fixups, заголовок и поля vp/fp/tp;
  - индекс в памяти отсекает промахи без обращения к SD;
  - compute и stream output не кэшируются;
  - счётчики `nvc0_code_cache_take_stats()` (их читает загрузчик).
- `codegen/nv50_ir_target.cpp`: сериализация relocs и fixups.
  В fixups лежат указатели на функции эмиттера GM107; они сохраняются номерами по таблице
  `gm107FixupApply`. Программа с другим указателем не кэшируется.
- `src/util/u_thread.h`: результат `thrd_create` сравнивается с `thrd_success`. В newlib он
  равен 4, и до r72 запущенный поток считался ошибкой. Тогда `util_queue_init` обнулял
  очередь, `util_queue_add_job` выбрасывал все записи, и `input` потока освобождался дважды.
  В r69–r71 из-за этого `mesa_shader_cache` оставался пустым.
  `disk_cache_create` теперь проверяет `util_queue_init`: без потока файлового хранилища нет.
- `include/c11/threads.h`: запасной `timespec_get` переименован. Новая newlib объявляет
  его в `<time.h>`, но реализации в `libc.a` нет.

## Сборка

Каталог `%USERPROFILE%\mesa-build`:
- `src/` — архив Mesa и патчи devkitPro (sha256 сверены с PKGBUILD);
- `mesa-20.1.0-rc3/` — дерево с git: коммиты `orig` и `dkp`, наши правки — рабочие изменения;
- `crossfile.txt` — повтор `meson-cross.sh switch` с путями Windows;
- `pc/libdrm_nouveau.pc` — с абсолютными путями;
- `py312-nopth/` — копия `pytools/py312` без `python312._pth`: генераторам Mesa нужен
  каталог скрипта в `sys.path`.

```bash
bash ~/mesa-build/configure.sh
JOBS=2 bash ~/mesa-build/build-switch.sh
```

Затем скопировать `%USERPROFILE%\mesa-build\out\lib\*.a` в `third_party/mesa-nx/lib/`.
Инструменты: meson 1.12, ninja 1.13, winflexbison (`%USERPROFILE%\tools\winflexbison`),
devkitA64 GCC 15.2. Больше 2–3 потоков не ставить: на ПК мало памяти.

## Память при переходах (r88)

После патча кэша применяется `switch-mesa-memory.patch`.
Он проверяет отказ `mm_slab_new`, откатывает неудачное выделение token,
проверяет смену backing буфера и добавляет `nouveau_nx_trim_memory`.
Загрузчик вызывает его только на GLES-потоке после `glFinish`, по запросу
экрана загрузки. Удаляются только полностью свободные slab; живые буферы
сохраняют свои ссылки. При OOM свободный кэш удаляется перед одной повторной попыткой.

Проверка с искусственными отказами, на фактическом исходнике `nouveau_mm.c`:

```text
python tools/test_mesa_memory.py --mesa-source <Mesa> --cc <C-compiler>
```

Причина исправления: crash report `01792914857_01005ce3b1a40000.log`, r87,
`memcpy <- u_default_buffer_subdata <- st_bufferobj_data`, перед ним GL_OUT_OF_MEMORY
при запросе `campaign_scenes_level2-1.bundle`. Аппаратная проверка исправления нужна.
