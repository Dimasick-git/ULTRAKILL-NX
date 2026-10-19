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
  CONFIG_VAR_INT(physics_hz); \
  CONFIG_VAR_INT(saltynx); \
  CONFIG_VAR_INT(loading_boost); \
  CONFIG_VAR_INT(fsr); \
  CONFIG_VAR_FLOAT(fsr_sharpness);

Config config;
static int config_needs_rewrite = 0;

// actual screen size that is in use right now
int screen_width = 1280;
int screen_height = 720;

static inline void parse_var(const char *name, const char *value) {
  // retired options -> drop them and rewrite the file without them
  if (!strcmp(name, "portrait") || !strcmp(name, "language")) {
    config_needs_rewrite = 1;
    return;
  }

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
  config.fsr_sharpness = 0.5f;

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
  if (config.physics_hz && (config.physics_hz < 60 || config.physics_hz > 125)) {
    config.physics_hz = 0;config_needs_rewrite = 1;
  }
  if (!(config.fsr_sharpness >= 0.0f && config.fsr_sharpness <= 2.0f)) {
    config.fsr_sharpness = 0.5f;
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
    "# fsr -- 1 = experimental FSR1 EASU+RCAS (requires r26 bundle); default 0\n"
    "# fsr_sharpness -- RCAS attenuation 0..2; 0 strongest, default 0.5\n"
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
