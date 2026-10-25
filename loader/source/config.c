/* config.c -- config.txt порта ULTRAKILL
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "nx_fg_build.h"
#include "util.h"

#define CONFIG_VARS \
  CONFIG_VAR_INT(docked_1080p); \
  CONFIG_VAR_INT(gyro); \
  CONFIG_VAR_FLOAT(gyro_sens); \
  CONFIG_VAR_INT(gyro_invert_x); \
  CONFIG_VAR_INT(gyro_invert_y); \
  CONFIG_VAR_INT(shader_chunks); \
  CONFIG_VAR_INT(performance_mode); \
  CONFIG_VAR_INT(render_height); \
  CONFIG_VAR_FLOAT(physics_max_dt); \
  CONFIG_VAR_INT(engine_profiler); \
  CONFIG_VAR_INT(frame_stats); \
  CONFIG_VAR_INT(swap_interval); \
  CONFIG_VAR_INT(collision_reuse); \
  CONFIG_VAR_INT(worker_core0); \
  CONFIG_VAR_INT(core3); \
  CONFIG_VAR_INT(gl_program_cache); \
  CONFIG_VAR_INT(fps_limit); \
  CONFIG_VAR_INT(autoaim_every); \
  CONFIG_VAR_INT(sky_every); \
  CONFIG_VAR_INT(physics_solver); \
  CONFIG_VAR_INT(physics_sync); \
  CONFIG_VAR_INT(reproject); \
  CONFIG_VAR_INT(ui_language); \
  CONFIG_VAR_INT(ui_language_revision); \
  CONFIG_VAR_INT(saltynx); \
  CONFIG_VAR_INT(loading_boost); \
  CONFIG_VAR_INT(fsr); \
  CONFIG_VAR_INT(fsr3); \
  CONFIG_VAR_INT(fg_probe); \
  CONFIG_VAR_INT(frame_generation); \
  CONFIG_VAR_INT(spawn_warmup); \
  CONFIG_VAR_FLOAT(fsr_sharpness);

Config config;
static int config_needs_rewrite = 0;

// actual screen size that is in use right now
int screen_width = 1280;
int screen_height = 720;

static int seen_physics_max_dt = 0;
static int seen_frame_stats = 0;
static int seen_swap_interval = 0;
static int seen_gl_program_cache = 0;
static int seen_fps_limit = 0;
static int seen_physics_solver = 0;
static int seen_physics_sync = 0;
static int seen_reproject = 0;
static int seen_ui_language = 0;

static inline void parse_var(const char *name, const char *value) {
  // retired options -> drop them and rewrite the file without them
  /* profile_sampler старые сборки записывали единицей -- ключ снят, чтобы он не
   * включал профайлер (теперь engine_profiler, default 0). physics_hz менял
   * скорость игры и давно не применяется; physics_steal не дал выигрыша. */
  if (!strcmp(name, "portrait") || !strcmp(name, "language") || !strcmp(name, "profile_sampler") ||
      !strcmp(name, "physics_hz") || !strcmp(name, "physics_steal")) {
    config_needs_rewrite = 1;
    return;
  }
  if (!strcmp(name, "physics_max_dt")) seen_physics_max_dt = 1;
  if (!strcmp(name, "physics_solver")) seen_physics_solver = 1;
  if (!strcmp(name, "physics_sync")) seen_physics_sync = 1;
  if (!strcmp(name, "reproject")) seen_reproject = 1;
  if (!strcmp(name, "ui_language")) seen_ui_language = 1;
  if (!strcmp(name, "frame_stats")) {
    seen_frame_stats = 1;
    /* 2 когда-то писалось по умолчанию (опыт с приоритетом потока рендера):
     * опыт закрыт, значение переводится в обычный замер */
    if (!strcmp(value, "2")) { config.frame_stats = 1; config_needs_rewrite = 1; return; }
  }
  if (!strcmp(name, "swap_interval")) seen_swap_interval = 1;
  if (!strcmp(name, "gl_program_cache")) seen_gl_program_cache = 1;
  if (!strcmp(name, "fps_limit")) seen_fps_limit = 1;

  #define CONFIG_VAR_INT(var) if (!strcmp(name, #var)) { config.var = atoi(value); return; }
  #define CONFIG_VAR_FLOAT(var) if (!strcmp(name, #var)) { config.var = (float)atof(value); return; }
  #define CONFIG_VAR_STR(var) if (!strcmp(name, #var)) { strlcpy(config.var, value, sizeof(config.var)); return; }
  CONFIG_VARS
  #undef CONFIG_VAR_INT
  #undef CONFIG_VAR_FLOAT
  #undef CONFIG_VAR_STR
}

int read_config(const char *file) {
  char line[1024] = { 0 };

  memset(&config, 0, sizeof(Config));
  config_needs_rewrite = 0;
  config.portrait = 0;
  config.docked_1080p = 0;
  config.gyro = 1;
  config.gyro_sens = 2.0f;
  /* Hardware test: CameraController needs the opposite sign on both axes. */
  config.gyro_invert_x = 1;
  config.gyro_invert_y = 1;
  config.shader_chunks = 16;
  config.performance_mode = 1;
  config.saltynx = 1;
  config.loading_boost = 1;
  config.render_height = 540;
  config.fsr = 0;
  config.frame_generation = 0; /* Experimental path currently costs more than a real frame. */
  config.spawn_warmup = 1;
  config.fsr_sharpness = 0.5f;
  /* Потолок шага кадра. Шаг физики игры 0.008 с (125 Гц), а движковый потолок
   * 0.1 с: на медленном кадре Unity догоняла до 12 шагов физики, и в бою это
   * углубляло просадку. 0.05 с -- до 20 FPS игра идёт в реальном времени, ниже
   * замедляется. При 0.034 картинка заметно замедлялась уже на 21-29 FPS. */
  config.physics_max_dt = 0.05f;
  seen_physics_max_dt = 0;
  /* Профайлер выключен по умолчанию: он 250 раз в секунду останавливает
   * UnityMain и поток рендера и сканирует их стек -- это само по себе отнимает FPS. */
  config.engine_profiler = 0;
  /* Замер фаз кадра через таблицу колбэков player loop -- два чтения таймера на
   * фазу, потоки не останавливает. На этих же обёртках держатся синхронизация
   * физики (physics_sync) и перепроецирование (reproject). */
  config.frame_stats = 1;
  seen_frame_stats = 0;
  /* при просадке ниже 60 FPS не ждать развёртку (см. nx_frame_present) */
  config.swap_interval = 2;
  seen_swap_interval = 0;
  /* один объект Collision на все колбэки столкновений: при 125 Гц физики это
   * главный источник мусора для GC (пауз было в 20 раз больше) */
  config.collision_reuse = 1;
  /* задачи физики на ядре 0, пока главный поток их ждёт (nx_cpu.c) */
  config.worker_core0 = 1;
  /* NPDM форвардера открывает ядро 3 (nx_cpu.c) */
  config.core3 = 1;
  /* Mesa с дисковым кэшем -> GL_NUM_PROGRAM_BINARY_FORMATS = 1, Unity
   * сохраняет собранные программы в UnityShaderCache (nx_shader_cache_setup) */
  config.gl_program_cache = 1;
  seen_gl_program_cache = 0;
  /* лимит решает переключатель частоты кадров в игре (по умолчанию 60),
   * «выкл» -- без ограничения (main.c, nx_fps_enforce) */
  config.fps_limit = 0;
  seen_fps_limit = 0;
  /* автоприцел ищет цель через кадр (nx_game_fixes.c) */
  config.autoaim_every = 2;
  /* небо Лимбо через кадр, пока FPS ниже 90 (nx_game_fixes.c) */
  config.sky_every = 0;
  /* 4 итерации решателя (как в самом PhysX) вместо 6 из проекта (main.c) */
  config.physics_solver = 4;
  seen_physics_solver = 0;
  /* физика синхронизируется перед фазами скриптов, а не на каждом запросе (nx_frame.c) */
  config.physics_sync = 1;
  seen_physics_sync = 0;
  /* перепроецирование кадра по повороту камеры (nx_warp.c) */
  config.reproject = 1;
  seen_reproject = 0;
  /* English until the user selects a language in General settings. */
  config.ui_language = 1;
  config.ui_language_revision = 0;
  seen_ui_language = 0;

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  // parse lines of the forms
  // <spaces> # <whatever> \n
  // <spaces> NAME <spaces> VALUE <spaces> \n
  do {
    char *name = NULL, *value = NULL, *tmp = NULL;
    if (fgets(line, sizeof(line), f) != NULL) {
      name = line;
      // trim name
      while (*name && isspace((int)*name)) ++name;
      if (name[0] == '#') continue; // skip comments
      for (tmp = name; *tmp && !isspace((int)*tmp); ++tmp);
      // if tmp points to the end of the string, there's no value to parse
      if (*tmp != 0) {
        *tmp = 0;
        // value is next; trim value
        for (value = tmp + 1; *value && isspace((int)*value); ++value);
        for (tmp = value + strlen(value) - 1; isspace((int)*tmp); --tmp) *tmp = 0;
        // got key value pair
        parse_var(name, value);
      }
    }
  } while (!feof(f));

  fclose(f);

  if (config.shader_chunks < 1 || config.shader_chunks > 32) {
    config.shader_chunks = 16;
    config_needs_rewrite = 1;
  }
  if (config.render_height < 180 || config.render_height > 720) {
    config.render_height = 540;
    config_needs_rewrite = 1;
  }

  if (config.gyro_sens <= 0.0f || config.gyro_sens > 20.0f) {
    config.gyro_sens = 2.0f;
    config_needs_rewrite = 1;
  }
  config.fsr = config.fsr ? 1 : 0;
  config.fsr3 = config.fsr3 ? 1 : 0;
  config.fg_probe = config.fg_probe ? 1 : 0;
  config.frame_generation = config.frame_generation ? 1 : 0;
#if !NX_EXPERIMENTAL_FRAMEGEN
  /* Stable GLES builds expose FSR 1 only. Retire persisted experimental
   * temporal requests too: otherwise an old menu choice hangs the next boot. */
  if (config.fsr3 || config.frame_generation || config.fg_probe || config.reproject) config_needs_rewrite = 1;
  config.fsr3 = config.frame_generation = config.fg_probe = 0;
  config.reproject = 0; /* No unused history/copy textures in the FSR 1 build. */
#endif
  config.spawn_warmup = config.spawn_warmup ? 1 : 0;
  if (!(config.fsr_sharpness >= 0.0f && config.fsr_sharpness <= 2.0f)) {
    config.fsr_sharpness = 0.5f;
    config_needs_rewrite = 1;
  }
  if (!(config.physics_max_dt >= 0.016f && config.physics_max_dt <= 0.1f)) {
    config.physics_max_dt = 0.05f;
    config_needs_rewrite = 1;
  }
  /* прежнее значение по умолчанию -> новое: старые config.txt его уже
   * записали, и само оно не поменялось бы */
  if (config.physics_max_dt > 0.0339f && config.physics_max_dt < 0.0341f) {
    config.physics_max_dt = 0.05f;
    config_needs_rewrite = 1;
  }
  if (!seen_physics_max_dt) config_needs_rewrite = 1;   /* показать новый ключ в файле */
  config.engine_profiler = config.engine_profiler ? 1 : 0;
  if (config.frame_stats < 0 || config.frame_stats > 2) {
    config.frame_stats = 1;
    config_needs_rewrite = 1;
  }
  if (!seen_frame_stats) config_needs_rewrite = 1;   /* показать новый ключ в файле */
  if (config.swap_interval < 0 || config.swap_interval > 2) {
    config.swap_interval = 2;
    config_needs_rewrite = 1;
  }
  if (!seen_swap_interval) config_needs_rewrite = 1;
  config.collision_reuse = config.collision_reuse ? 1 : 0;
  config.worker_core0 = config.worker_core0 ? 1 : 0;
  config.core3 = config.core3 ? 1 : 0;
  if (config.gl_program_cache < 0 || config.gl_program_cache > 2) {
    config.gl_program_cache = 1;
    config_needs_rewrite = 1;
  }
  if (!seen_gl_program_cache) config_needs_rewrite = 1;
  if (config.fps_limit && (config.fps_limit < 30 || config.fps_limit > 1000)) {
    config.fps_limit = 0;
    config_needs_rewrite = 1;
  }
  if (!seen_fps_limit) config_needs_rewrite = 1;
  if (config.autoaim_every < 1 || config.autoaim_every > 4) {
    config.autoaim_every = 2;
    config_needs_rewrite = 1;
  }
  if (config.sky_every < 0 || config.sky_every > 3) {
    config.sky_every = 0;
    config_needs_rewrite = 1;
  }
  if (config.physics_solver < 0 || config.physics_solver > 20) {
    config.physics_solver = 4;
    config_needs_rewrite = 1;
  }
  if (!seen_physics_solver) config_needs_rewrite = 1;
  config.physics_sync = config.physics_sync ? 1 : 0;
  if (!seen_physics_sync) config_needs_rewrite = 1;
  config.reproject = config.reproject ? 1 : 0;
  if (!seen_reproject) config_needs_rewrite = 1;
  if (config.ui_language < 0 || config.ui_language > 14) {
    config.ui_language = 1;
    config_needs_rewrite = 1;
  }
  if (!seen_ui_language) config_needs_rewrite = 1;
  if (config.ui_language_revision < 1) {
    if (config.ui_language == 0) config.ui_language = 1;
    config.ui_language_revision = 1;
    config_needs_rewrite = 1;
  }

  return config_needs_rewrite ? 1 : 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  fprintf(f,
    "# ultrakill_nx -- строки вида \"имя значение\"; # начинает комментарий\n"
    "#\n"
    "# docked_1080p  -- 1 = в доке рендер 1920x1080, 0 = 1280x720 (быстрее)\n"
    "# gyro          -- 1 = гироприцел, 0 = выключен\n"
    "# gyro_sens     -- чувствительность: 1.0 = камера поворачивается на тот же угол,\n"
    "#                  что и консоль; по умолчанию 2.0\n"
    "# gyro_invert_x -- 1 = развернуть горизонталь гироскопа\n"
    "# gyro_invert_y -- 1 = инвертировать вертикаль гироскопа\n"
    "# shader_chunks -- кэш шейдеров: 1..32 блоков по 16 MiB; default 16\n"
    "# performance_mode -- 1 = профиль 60 FPS с облегчённой графикой\n"
    "# render_height -- внутренний рендер 360/480/540/720; default 540\n"
    "# fsr -- 1 = experimental FSR1 EASU+RCAS (requires the FSR bundle); default 0\n"
    "# fsr3 -- reserved; disabled in the stable FSR 1 renderer\n"
    "# fsr_sharpness -- RCAS attenuation 0..2; 0 strongest, default 0.5\n"
    "# physics_max_dt -- потолок шага кадра, с (0.016..0.1); до 1/значение FPS игра\n"
    "#                  идёт в реальном времени, ниже замедляется вместо обвала;\n"
    "#                  0.1 = как в оригинале, default 0.05\n"
    "# engine_profiler -- 1 = профайлер движка в debug.log (строки [prof]); отнимает FPS, default 0\n"
    "# frame_stats   -- фазы кадра в debug.log (строки [frame]): 0 = выкл, 1 = замер; default 1\n"
    "# swap_interval -- ожидание развёртки при показе кадра: 0 = не ждать, 1 = ждать всегда,\n"
    "#                  2 = ждать только пока держится 60 FPS; default 2\n"
    "# collision_reuse -- 1 = физика переиспользует объект столкновения (меньше пауз GC); default 1\n"
    "# worker_core0  -- 1 = потоки задач занимают ядро 0, пока главный поток ждёт; default 1\n"
    "# core3         -- 1 = ядро 3 (системное) для потоков задач и фона; 0 = только ядра 0-2; default 1\n"
    "# fps_limit     -- 0 = лимит из настроек игры (частота кадров, по умолчанию 60; «выкл» = без\n"
    "#                  ограничения), default 0; число, например 60 = свой лимит поверх игры\n"
    "# autoaim_every -- автоприцел ищет цель раз в N кадров: 1 = каждый кадр (как в игре), default 2\n"
    "# sky_every     -- небо на уровнях Лимбо: 1 = каждый кадр (как в игре), 2 = через кадр,\n"
    "#                  0 = через кадр, пока FPS ниже 90; default 0\n"
    "# physics_solver -- итерации решателя физики: 0 = как в игре (6), меньше = быстрее шаг\n"
    "#                  физики, тела чуть мягче в завалах; default 4\n"
    "# physics_sync  -- 1 = положения объектов передаются физике перед скриптами FixedUpdate,\n"
    "#                  Update, корутинами и LateUpdate; 0 = как в игре, перед каждым лучом\n"
    "#                  (до 66 раз за кадр в бою); default 1\n"
    "# reproject     -- прототип: когда игра ниже 60 FPS, между кадрами показывается последний\n"
    "#                  кадр, повёрнутый по движению камеры (оружие и интерфейс на месте);\n"
    "#                  0 = выкл.; default 1\n"
    "# ui_language   -- язык текстов игры: 0 = как в системе Switch, 1 English, 2 Русский,\n"
    "#                  3 Українська, 4 Deutsch, 5 Français, 6 Español, 7 Português (Brasil),\n"
    "#                  8 Italiano, 9 Polski, 10 Türkçe, 11 日本語, 12 한국어, 13 简体中文,\n"
    "#                  14 繁體中文; default 1 (General / Language)\n"
    "# gl_program_cache -- кэш собранных шейдеров: 0 = выкл. (каждый запуск собирает заново),\n"
    "#                  1 = программы Unity (UnityShaderCache) и машинный код GPU\n"
    "#                  (mesa_shader_cache), 2 = ещё и GLSL-кэш Mesa (опыт); default 1\n"
    "#\n"
    "\n");

  #define CONFIG_VAR_INT(var) fprintf(f, "%s %d\n", #var, config.var)
  #define CONFIG_VAR_FLOAT(var) fprintf(f, "%s %g\n", #var, (double)config.var)
  #define CONFIG_VAR_STR(var) if (config.var[0]) fprintf(f, "%s %s\n", #var, config.var)
  CONFIG_VARS
  #undef CONFIG_VAR_INT
  #undef CONFIG_VAR_FLOAT
  #undef CONFIG_VAR_STR

  int failed = ferror(f);
  if (fclose(f) != 0) failed = 1;
  return failed ? -1 : 0;
}
