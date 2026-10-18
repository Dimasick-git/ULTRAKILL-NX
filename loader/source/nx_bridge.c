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
 * ЗНАКИ. Поперечная ось и знак тангажа взяты из nx_pointer порта Papers,
 * Please, где они проверены на железе: у Pro-контроллера оси развёрнуты на
 * 180 градусов относительно Joy-Con. Проверка ULTRAKILL на железе показала
 * обратное направление обеих осей: gyro_invert_x/y по умолчанию равны 1.
 * Первые выборки пишутся в debug.log вместе с вектором «вверх». */

#include <switch.h>
#include <math.h>
#include <string.h>

#include "nx_bridge.h"
#include "config.h"
#include "util.h"

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

int nxb_config_int(const char *key) {
  if (!key) return 0;
  if (!strcmp(key, "gyro"))          return config.gyro;
  if (!strcmp(key, "gyro_invert_x")) return config.gyro_invert_x;
  if (!strcmp(key, "gyro_invert_y")) return config.gyro_invert_y;
  if (!strcmp(key, "docked_1080p"))  return config.docked_1080p;
  return 0;
}

float nxb_config_float(const char *key) {
  if (key && !strcmp(key, "gyro_sens")) return config.gyro_sens;
  return 0.0f;
}

void nxb_log(const char *msg) {
  if (msg) debugPrintf("[cs] %s\n", msg);
}
