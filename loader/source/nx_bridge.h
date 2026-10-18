/* nx_bridge.h -- то, что C#-слой нашей сборки ULTRAKILL берёт у загрузчика.
 *
 * На стороне C#: [DllImport("nxbridge")] static extern int nxb_pad(ref NxbPad p);
 * IL2CPP на Android делает dlopen("libnxbridge.so") + dlsym("nxb_pad"); наши
 * dlopen_fake/dlsym_fake находят эти функции в таблице импортов (imports.c).
 * На настоящем Android библиотеки нет -> DllNotFoundException, C# это ловит.
 *
 * Раскладка структур -- контракт с unity/Assets/NxPort/NxBridge.cs: менять
 * только вместе с NXB_VERSION и с C#-стороной. */
#ifndef NX_BRIDGE_H
#define NX_BRIDGE_H

#include <stdint.h>

#define NXB_VERSION 1

/* Кнопки -- биты HidNpadButton из libnx как есть (A=bit0, B=1, X=2, Y=3,
 * StickL=4, StickR=5, L=6, R=7, ZL=8, ZR=9, Plus=10, Minus=11, Left=12,
 * Up=13, Right=14, Down=15). */
typedef struct {
  uint32_t version;     /* NXB_VERSION */
  uint32_t connected;   /* 1, если есть хоть один контроллер */
  uint64_t buttons;     /* зажатые кнопки */
  float    lx, ly;      /* левый стик  -1..1, вверх = +1 */
  float    rx, ry;      /* правый стик -1..1, вверх = +1 */
  uint32_t style;       /* HidNpadStyleTag текущего контроллера */
  uint32_t docked;      /* 1 = в доке */
} NxbPad;

/* yaw/pitch уже умножены на gyro_sens и развёрнуты по gyro_invert_x/y из
 * config.txt -- C# прибавляет их к камере как есть. */
typedef struct {
  uint32_t valid;       /* 1, если датчик дал свежие выборки */
  float    yaw;         /* поворот вправо с прошлого вызова, градусы */
  float    pitch;       /* наклон вверх с прошлого вызова, градусы   */
  float    rate;        /* модуль угловой скорости сейчас, град/с     */
  uint32_t samples;     /* сколько выборок датчика вошло в этот вызов */
} NxbMotion;

/* main.c: один раз после SDL/HID и раз в кадр до nativeRender. */
void nxb_init(void);
void nxb_frame(void);

/* Экспорт для C# (регистрируются в imports.c). */
int   nxb_version(void);
int   nxb_pad(NxbPad *out);
int   nxb_motion(NxbMotion *out);
int   nxb_config_int(const char *key);
float nxb_config_float(const char *key);
void  nxb_log(const char *msg);

#endif
