/* nx_bridge.c -- геймпад и гироскоп для C#-слоя. См. nx_bridge.h.
 *
 * ГИРОСКОП. Датчик отдаёт угловую скорость (обороты/с) пачкой выборок
 * примерно 200 раз в секунду, а кадр игры идёт 30-60 раз. Брать одну
 * последнюю выборку на кадр и умножать на время кадра -- значит терять
 * короткие рывки кистью. Поэтому nxb_frame() проходит ВСЕ новые выборки по
 * sampling_number и интегрирует каждую по её собственному delta_time.
 *
 * Рыскание считаем в «пространстве игрока»: ось поворота -- мировая
 * вертикаль, её даёт акселерометр (сглаженный вектор «вверх»). Так поворот
 * влево-вправо одинаков, держишь ли консоль стоймя в руках или Pro-контроллер
 * плашмя -- локальная ось рыскания у этих хватов разная, а мировая одна.
 * Тангаж -- вокруг поперечной оси самого контроллера.
 *
 * ЗНАКИ. У Pro-контроллера оси развёрнуты на 180 градусов относительно
 * Joy-Con. Проверка на железе показала обратное направление обеих осей:
 * gyro_invert_x/y по умолчанию равны 1.
 * Первые выборки пишутся в debug.log вместе с вектором «вверх». */

#include <switch.h>
#include <math.h>
#include <string.h>

#include "nx_bridge.h"
#include "nx_fg.h"
#include "nx_render_control.h"
#include "nx_gameroot.h"
#include "nx_loading.h"
#include <stdio.h>
#include "config.h"
#include "util.h"
#include <GLES3/gl3.h>

/* The request crosses threads; Mesa's slab lists are touched only by the
 * current GLES render thread, after all preceding GPU work has finished. */
extern uint64_t nouveau_nx_trim_memory(void);
static unsigned trim_requested, trim_completed;
static unsigned render_faults;
void nxb_render_fault_report(unsigned faults) { __atomic_fetch_or(&render_faults, faults, __ATOMIC_RELAXED); }
unsigned nxb_render_faults(void) { return __atomic_exchange_n(&render_faults, 0, __ATOMIC_ACQ_REL); }
unsigned nxb_memory_trim(int request) {
  return request ? __atomic_add_fetch(&trim_requested, 1, __ATOMIC_ACQ_REL)
                 : __atomic_load_n(&trim_completed, __ATOMIC_ACQUIRE);
}
void nxb_memory_trim_render(void) {
  unsigned ticket = __atomic_load_n(&trim_requested, __ATOMIC_ACQUIRE);
  if (ticket == __atomic_load_n(&trim_completed, __ATOMIC_RELAXED)) return;
  glFinish();
  uint64_t bytes = nouveau_nx_trim_memory();
  debugPrintf("[memory] transition trim #%u: unused Mesa slabs %llu KB\n", ticket,
              (unsigned long long)(bytes >> 10));
  __atomic_store_n(&trim_completed, ticket, __ATOMIC_RELEASE);
}

#define RAD2DEG       57.29577951308232f
#define REV2DEG       360.0f      /* угловая скорость HID -- в оборотах/с */
#define GYRO_DEADZONE 0.75f       /* град/с: покой не должен ползти */
#define UP_SMOOTH     0.02f       /* доля новой выборки акселерометра в «вверх» */

static PadState s_pad;
static HidSixAxisSensorHandle s_six[4];   /* 0 handheld, 1 Pro, 2/3 левый/правый Joy-Con */
static int      s_ready = 0;

/* накопленное между вызовами nxb_motion */
static float    s_acc_yaw, s_acc_pitch, s_rate;
static uint32_t s_acc_samples;
static int      s_frames_unread;

static u64      s_last_sampling[4];
static float    s_up[3] = { 0.0f, 0.0f, 1.0f };
static int      s_up_valid = 0;
static int      s_logged = 0;

void nxb_init(void) {
  if (s_ready) return;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&s_pad);

  hidGetSixAxisSensorHandles(&s_six[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  hidGetSixAxisSensorHandles(&s_six[1], 1, HidNpadIdType_No1,      HidNpadStyleTag_NpadFullKey);
  hidGetSixAxisSensorHandles(&s_six[2], 2, HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyDual);
  for (int i = 0; i < 4; i++) hidStartSixAxisSensor(s_six[i]);

  s_ready = 1;
  debugPrintf("[nxb] init: bridge v%d, gyro=%d sens=%.2f invert_x=%d invert_y=%d\n",
              NXB_VERSION, config.gyro, (double)config.gyro_sens,
              config.gyro_invert_x, config.gyro_invert_y);
}

/* Какой датчик читать при текущем хвате и знак его поперечной оси. */
static int pick_sensor(float *lat_sign) {
  const u64 style = padGetStyleSet(&s_pad);
  *lat_sign = +1.0f;                                   /* оси Joy-Con */
  if (style & HidNpadStyleTag_NpadFullKey) { *lat_sign = -1.0f; return 1; }
  if (style & HidNpadStyleTag_NpadHandheld) return 0;
  if (style & HidNpadStyleTag_NpadJoyDual) {
    const u64 attr = padGetAttributes(&s_pad);
    if (attr & HidNpadAttribute_IsRightConnected) return 3;  /* целятся правой */
    if (attr & HidNpadAttribute_IsLeftConnected)  return 2;
  }
  return -1;
}

static void integrate_motion(void) {
  float lat_sign;
  const int idx = pick_sensor(&lat_sign);
  if (idx < 0) return;

  HidSixAxisSensorState st[16];
  const size_t n = hidGetSixAxisSensorStates(s_six[idx], st, 16);
  if (!n) return;

  /* Выборки идут от новой к старой; проходим от старой к новой. */
  for (int i = (int)n - 1; i >= 0; i--) {
    const HidSixAxisSensorState *s = &st[i];
    if (s->sampling_number <= s_last_sampling[idx]) continue;
    const int first = (s_last_sampling[idx] == 0);
    s_last_sampling[idx] = s->sampling_number;
    if (first) continue;                 /* без опорной точки dt не известен */

    float dt = (float)s->delta_time / 1e9f;
    if (dt <= 0.0f || dt > 0.05f) dt = 0.005f;

    /* «вверх» из акселерометра, сглаженный */
    const float ax = s->acceleration.x, ay = s->acceleration.y, az = s->acceleration.z;
    const float am = sqrtf(ax * ax + ay * ay + az * az);
    if (am > 0.5f && am < 1.5f) {        /* в рывке акселерометр врёт про вертикаль */
      const float k = s_up_valid ? UP_SMOOTH : 1.0f;
      s_up[0] += (ax / am - s_up[0]) * k;
      s_up[1] += (ay / am - s_up[1]) * k;
      s_up[2] += (az / am - s_up[2]) * k;
      const float um = sqrtf(s_up[0] * s_up[0] + s_up[1] * s_up[1] + s_up[2] * s_up[2]);
      if (um > 1e-3f) { s_up[0] /= um; s_up[1] /= um; s_up[2] /= um; }
      s_up_valid = 1;
    }

    const float gx = s->angular_velocity.x * REV2DEG;   /* град/с */
    const float gy = s->angular_velocity.y * REV2DEG;
    const float gz = s->angular_velocity.z * REV2DEG;
    const float rate = sqrtf(gx * gx + gy * gy + gz * gz);
    s_rate = rate;
    if (rate < GYRO_DEADZONE) continue;

    float yaw_rate;
    if (s_up_valid)
      yaw_rate = -(gx * s_up[0] + gy * s_up[1] + gz * s_up[2]);
    else
      yaw_rate = lat_sign * gy;                       /* до первой выборки акселерометра */
    const float pitch_rate = -lat_sign * gx;

    s_acc_yaw   += yaw_rate * dt;
    s_acc_pitch += pitch_rate * dt;
    s_acc_samples++;

    if (s_logged < 8) {
      s_logged++;
      debugPrintf("[nxb] gyro #%d sensor=%d av=(%.1f %.1f %.1f) dps acc=(%.2f %.2f %.2f) "
                  "up=(%.2f %.2f %.2f) dt=%.4f -> yaw %.2f pitch %.2f dps\n",
                  s_logged, idx, (double)gx, (double)gy, (double)gz,
                  (double)ax, (double)ay, (double)az,
                  (double)s_up[0], (double)s_up[1], (double)s_up[2], (double)dt,
                  (double)yaw_rate, (double)pitch_rate);
    }
  }
}

void nxb_frame(void) {
  if (!s_ready) return;
  padUpdate(&s_pad);
  if (config.gyro) integrate_motion();

  /* Если C# не забирает накопленное (меню, загрузка), не копим рывок на потом:
   * через три кадра без чтения сбрасываем. */
  if (++s_frames_unread > 3) {
    s_acc_yaw = s_acc_pitch = 0.0f;
    s_acc_samples = 0;
  }
}

/* ----------------------------------------------------------- экспорт для C# */

int nxb_version(void) { return NXB_VERSION; }

int nxb_touch_ui_active;
int nxb_touch(NxbTouch *out) {
  static uint32_t finger;
  static float last_x, last_y;
  static int held;
  if (!out) return 0;
  nxb_touch_ui_active = 1; /* Managed UI owns taps: no legacy double injection. */
  memset(out, 0, sizeof *out);
  /* HID has no contact coordinates after lift. UI hit-testing still needs
   * the last primary position for pointer-up/click, not a jump to (0,0). */
  out->id=finger; out->x=last_x; out->y=last_y;
  HidTouchScreenState state;
  if (hidGetTouchScreenStates(&state, 1) <= 0 || !state.count) {
    held = 0; return 1;
  }
  for (unsigned i=0; i<state.count && i<16; ++i) {
    if (held && state.touches[i].finger_id != finger) continue;
    finger = state.touches[i].finger_id; held = 1;
    out->down=1; out->id=finger;
    out->x=last_x=state.touches[i].x; out->y=last_y=state.touches[i].y;
    return 1;
  }
  /* Release the primary before handing ownership to another finger. */
  held=0; return 1;
}

static float stick(s32 v) {
  float f = (float)v / (float)JOYSTICK_MAX;
  return f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
}

int nxb_pad(NxbPad *out) {
  if (!out) return 0;
  memset(out, 0, sizeof *out);
  out->version = NXB_VERSION;
  out->docked  = appletGetOperationMode() == AppletOperationMode_Console;
  if (!s_ready) return 0;

  out->connected = padIsConnected(&s_pad) ? 1 : 0;
  out->buttons   = padGetButtons(&s_pad);
  const HidAnalogStickState l = padGetStickPos(&s_pad, 0);
  const HidAnalogStickState r = padGetStickPos(&s_pad, 1);
  out->lx = stick(l.x); out->ly = stick(l.y);
  out->rx = stick(r.x); out->ry = stick(r.y);
  out->style = (uint32_t)padGetStyleSet(&s_pad);
  return out->connected;
}

int nxb_motion(NxbMotion *out) {
  if (!out) return 0;
  memset(out, 0, sizeof *out);
  s_frames_unread = 0;
  if (!s_ready || !config.gyro) return 0;

  const float sx = config.gyro_invert_x ? -1.0f : 1.0f;
  const float sy = config.gyro_invert_y ? -1.0f : 1.0f;
  out->valid   = s_acc_samples > 0;
  out->yaw     = s_acc_yaw * sx * config.gyro_sens;
  out->pitch   = s_acc_pitch * sy * config.gyro_sens;
  out->rate    = s_rate;
  out->samples = s_acc_samples;
  s_acc_yaw = s_acc_pitch = 0.0f;
  s_acc_samples = 0;
  return (int)out->valid;
}

/* Язык системы Switch -> номер языка текстов (порядок -- NxLocLanguages в
 * NxLocalization.cs): 1 English, 2 Русский, 4 Deutsch, 5 Français, 6 Español,
 * 7 Português, 8 Italiano, 11 日本語, 12 한국어, 13 简体中文, 14 繁體中文.
 * Нидерландского нет -- английский. */
static int system_language(void) {
  u64 code = 0;
  SetLanguage lang = SetLanguage_ENUS;
  if (R_FAILED(setInitialize())) return 1;
  const int ok = R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &lang));
  setExit();
  if (!ok) return 1;
  switch (lang) {
    case SetLanguage_RU:                              return 2;
    case SetLanguage_DE:                              return 4;
    case SetLanguage_FR: case SetLanguage_FRCA:       return 5;
    case SetLanguage_ES: case SetLanguage_ES419:      return 6;
    case SetLanguage_PT: case SetLanguage_PTBR:       return 7;
    case SetLanguage_IT:                              return 8;
    case SetLanguage_JA:                              return 11;
    case SetLanguage_KO:                              return 12;
    case SetLanguage_ZHCN: case SetLanguage_ZHHANS:   return 13;
    case SetLanguage_ZHTW: case SetLanguage_ZHHANT:   return 14;
    default:                                          return 1;
  }
}

int nxb_config_int(const char *key) {
  if (!key) return 0;
  if (!strcmp(key, "ui_language"))   return config.ui_language;
  if (!strcmp(key, "performance_mode")) return config.performance_mode;
  if (!strcmp(key, "engine_profiler")) return config.engine_profiler;
  if (!strcmp(key, "system_language")) return system_language();
  if (!strcmp(key, "gyro"))          return config.gyro;
  if (!strcmp(key, "gyro_invert_x")) return config.gyro_invert_x;
  if (!strcmp(key, "gyro_invert_y")) return config.gyro_invert_y;
  if (!strcmp(key, "docked_1080p"))  return config.docked_1080p;
  if (!strcmp(key, "fsr"))           return config.fsr;
  if (!strcmp(key, "fsr3"))          return config.fsr3;
  if (!strcmp(key, "fg_probe"))      return config.fg_probe;
  if (!strcmp(key, "fg_available")) return nxb_fg_available();
  if (!strcmp(key, "frame_generation")) return config.frame_generation;
  if (!strcmp(key, "spawn_warmup")) return config.spawn_warmup;
  if (!strcmp(key, "fg_runtime")) return nx_fg_runtime_state();
  if (!strcmp(key, "render_height")) return nx_render_height();
  if (!strcmp(key, "fsr_enabled")) return nx_render_filter();
  if (!strcmp(key, "loading_boost")) return config.loading_boost;
  if (!strcmp(key, "support_reboot")) { extern int nx_support_reboot_required;return nx_support_reboot_required; }
  return 0;
}

float nxb_config_float(const char *key) {
  if (key && !strcmp(key, "gyro_sens")) return config.gyro_sens;
  if (key && !strcmp(key, "fsr_sharpness")) return nx_render_sharpness();
  return 0.0f;
}

void nxb_log(const char *msg) {
  if (msg) debugPrintf("[cs] %s\n", msg);
}

/* Only user-facing runtime options. Never expose heap/physics/clock internals. */
int nxb_config_set(const char *key, float value) {
  if (!key || !isfinite(value)) return 0;
  Config before=config;
  int render=0;
  if (!strcmp(key,"gyro_sens") && value>=0.1f && value<=20) config.gyro_sens=value;
  else if (!strcmp(key,"render_height") && value>=180 && value<=720 && value==(int)value) { config.render_height=(int)value;render=1; }
  else if (!strcmp(key,"fsr_sharpness") && value>=0 && value<=2) { config.fsr_sharpness=value;render=1; }
  else if (!strcmp(key,"ui_language") && value>=0 && value<=14 && value==(int)value) config.ui_language=(int)value;
  else if (value==0 || value==1) {
    int v=(int)value;
    if (!strcmp(key,"gyro")) config.gyro=v;
    else if (!strcmp(key,"gyro_invert_x")) config.gyro_invert_x=v;
    else if (!strcmp(key,"gyro_invert_y")) config.gyro_invert_y=v;
    else if (!strcmp(key,"loading_boost")) config.loading_boost=v;
    else if (!strcmp(key,"fsr")) { config.fsr=v;render=1; }
    else if (!strcmp(key,"fsr3")) {
#if !NX_EXPERIMENTAL_FRAMEGEN
      if (v) return 0;
#endif
      config.fsr3=v;
    }
    else if (!strcmp(key,"frame_generation")) {
      if (v && !nxb_fg_available()) return 0;
      config.frame_generation=v;
    }
    else if (!strcmp(key,"spawn_warmup")) config.spawn_warmup=v;
    else return 0;
  } else return 0;
  if (render) {
    /* Preserve an external FSR-NX request when editing just one option in the
     * game's panel; do not reset its other fields to stale file defaults. */
    Config requested=config;
    config.render_height=nx_render_height();
    config.fsr=nx_render_filter();
    config.fsr_sharpness=nx_render_sharpness();
    if (!strcmp(key,"render_height")) config.render_height=requested.render_height;
    if (!strcmp(key,"fsr")) config.fsr=requested.fsr;
    if (!strcmp(key,"fsr_sharpness")) config.fsr_sharpness=requested.fsr_sharpness;
  }
  char path[320],temp[336],backup[336];game_path(path,sizeof path,CONFIG_NAME);
  snprintf(temp,sizeof temp,"%s.tmp",path);
  snprintf(backup,sizeof backup,"%s.bak",path);
  int saved=write_config(temp)==0;
  if (saved && rename(temp,path)!=0) {
    /* Horizon's RenameFile can reject an existing destination. Keep a
     * recoverable old config until the complete new file is published. */
    remove(backup);
    if (rename(path,backup)==0) {
      saved=rename(temp,path)==0;
      if (!saved) rename(backup,path); else remove(backup);
    } else saved=0;
  }
  if (!saved) {
    config=before;remove(temp);return 0;
  }
  if (render) nx_render_config_changed();
  if (!strcmp(key,"frame_generation") && before.frame_generation!=config.frame_generation) nx_fg_settings_changed();
  if (!strcmp(key,"loading_boost")) nx_loading_settings_changed();
  return 1;
}
