/* nx_warp.c -- перепроецирование кадра по повороту камеры (прототип).
 *
 * Игра упирается в главный поток (30-45 FPS в бою), а видеочип и поток рендера
 * большую часть кадра простаивают. Пока поток рендера ждёт команды следующего
 * кадра, он показывает последний кадр, повёрнутый на тот поворот камеры, который
 * набежал бы к этому моменту (продолжение поворота между двумя последними
 * кадрами). Мир поворачивается, оружие и интерфейс стоят на месте. Сдвиг камеры
 * не учитывается, только поворот.
 *
 * Снимки за кадр -- на потоке рендера, через GLIssuePluginEvent: вызов встаёт в
 * поток команд движка ровно туда, где его выдал главный поток:
 *   мир    -- mainTex после основной камеры (до камеры HUD);
 *   оружие -- mainTex после камеры HUD (она рисует в ту же mainTex, SetupRTs);
 *   сцена  -- экран на входе UI::RenderOverlays (после всех камер);
 *   итог   -- экран перед eglSwapBuffers.
 * Где «оружие» отличается от «мира» -- оружие, где «итог» от «сцены» --
 * интерфейс: эти пиксели берутся без поворота. Остальное -- «сцена», повёрнутая
 * гомографией K * Rстар^T * Rнов * K^-1.
 *
 * Вставка: futex потока рендера (libc_shim.c) зовёт nx_warp_idle, пока поток
 * ждёт. Если с последнего показа прошло 16.7 мс, нового кадра нет и снимки
 * показанного целы (событие «мир» следующего кадра ещё не пришло), рисуется
 * повёрнутый кадр в FBO 0 и вызывается eglSwapBuffers. Состояние GL движка
 * сохраняется и восстанавливается: кэш состояний Unity о нас не знает. */
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "nx_offsets.h"
#include "nx_warp.h"
#include "util.h"

#define WARP_PERIOD_NS 16666667LL   /* кадр экрана 60 Гц */
#define WARP_MARGIN_NS 1000000LL    /* запас на отрисовку и показ вставки */
#define WARP_MAX_INSERT 3           /* вставок подряд без настоящего кадра */
#define WARP_MAX_ANGLE 0.35f        /* рад; больше -- рывок или телепорт, не поворачиваем */
#define WARP_STATS_FRAMES 600

typedef struct { float m[16]; } M44;                   /* Matrix4x4f, по столбцам */
typedef struct { float r[9]; float p00, p11, p02, p12; uint64_t tick; } Pose;   /* r по строкам */

enum { EV_WORLD = 1, EV_GUN, EV_SCENE };
enum { T_WORLD, T_GUN, T_SCENE, T_FINAL, T_COUNT };    /* он же номер текстурного блока */

/* --- главный поток ----------------------------------------------------------- */
static void (*g_issue)(void (*)(int), int);
static M44 (*g_cam_to_world)(uintptr_t);
static M44 (*g_cam_proj)(uintptr_t);
static Pose g_ring[8];
static unsigned g_ring_next;
static int g_main_slot = -1;

/* --- поток рендера ----------------------------------------------------------- */
static void *g_rt_tls;
static int g_ready, g_broken;
static GLuint g_tex[T_COUNT], g_fbo[T_COUNT], g_src_fbo, g_prog, g_vao;
static GLint g_u_h;
static int g_tw[T_COUNT], g_th[T_COUNT];
static GLuint g_main_tex;
static int g_main_w, g_main_h;
static int g_have[T_COUNT];          /* снимки кадра, который сейчас рисуется */
static int g_frame_slot = -1;
static int g_shown_valid, g_have_prev, g_last_complete;
static Pose g_shown, g_prev;
static uint64_t g_shown_present, g_last_present;
static int g_inserted;
static EGLDisplay g_dpy;
static EGLSurface g_surf;
static volatile int g_presenting;
static int g_trace = 3;
static unsigned s_real, s_insert, s_incomplete, s_still, s_jump;
static uint64_t s_insert_ticks, s_capture_ticks;

static void warp_event(int id);

/* ============================================================ главный поток */
void nx_warp_install(uintptr_t ub) {
  if (!config.reproject) {
    debugPrintf("[warp] перепроецирование выключено (reproject 0)\n");
    return;
  }
  const uint32_t issue = nxo_get("unity.gl_issue_plugin_event");
  const uint32_t c2w = nxo_get("unity.camera_to_world");
  const uint32_t proj = nxo_get("unity.camera_projection");
  if (!issue || !c2w || !proj || !config.frame_stats) {
    debugPrintf("[warp] нет unity.gl_issue_plugin_event/camera_* в offsets.ini или frame_stats 0 -- без перепроецирования\n");
    return;
  }
  g_cam_to_world = (M44 (*)(uintptr_t))(ub + c2w);
  g_cam_proj = (M44 (*)(uintptr_t))(ub + proj);
  g_issue = (void (*)(void (*)(int), int))(ub + issue);
  debugPrintf("[warp] перепроецирование по повороту камеры: вставка при FPS ниже 60\n");
}

int nx_warp_role(const char *name) {
  if (!g_issue || !name) return NX_WARP_CAM_NONE;
  if (!strcmp(name, "Main Camera")) return NX_WARP_CAM_MAIN;
  if (!strcmp(name, "HUD Camera")) return NX_WARP_CAM_HUD;
  return NX_WARP_CAM_NONE;
}

void nx_warp_camera(int role, uintptr_t camera, int exit) {
  if (!g_issue) return;
  if (role == NX_WARP_CAM_MAIN && !exit) {
    const M44 c = g_cam_to_world(camera), p = g_cam_proj(camera);
    const int slot = (int)(g_ring_next++ & 7);
    Pose *o = &g_ring[slot];
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 3; k++) o->r[r * 3 + k] = c.m[k * 4 + r];
    o->p00 = p.m[0]; o->p11 = p.m[5]; o->p02 = p.m[8]; o->p12 = p.m[9];
    o->tick = armGetSystemTick();
    g_main_slot = slot;
  } else if (role == NX_WARP_CAM_MAIN && g_main_slot >= 0) {
    g_issue(warp_event, EV_WORLD << 8 | g_main_slot);
    g_main_slot = -1;
  } else if (role == NX_WARP_CAM_HUD && exit) {
    g_issue(warp_event, EV_GUN << 8);
  }
}

void nx_warp_overlays(void) {
  if (g_issue) g_issue(warp_event, EV_SCENE << 8);
}

/* ============================================================ состояние GL */
typedef struct {
  GLint draw_fb, read_fb, program, vao, active, tex[T_COUNT], samp[T_COUNT], viewport[4];
  GLboolean scissor, blend, depth, stencil, cull, discard, mask[4];
} GlState;

static void gl_save(GlState *s) {
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s->draw_fb);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s->read_fb);
  glGetIntegerv(GL_CURRENT_PROGRAM, &s->program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
  for (int i = 0; i < T_COUNT; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex[i]);
    glGetIntegerv(GL_SAMPLER_BINDING, &s->samp[i]);
  }
  glGetIntegerv(GL_VIEWPORT, s->viewport);
  s->scissor = glIsEnabled(GL_SCISSOR_TEST);
  s->blend = glIsEnabled(GL_BLEND);
  s->depth = glIsEnabled(GL_DEPTH_TEST);
  s->stencil = glIsEnabled(GL_STENCIL_TEST);
  s->cull = glIsEnabled(GL_CULL_FACE);
  s->discard = glIsEnabled(GL_RASTERIZER_DISCARD);
  glGetBooleanv(GL_COLOR_WRITEMASK, s->mask);
}

static void gl_enable(GLenum cap, GLboolean on) { if (on) glEnable(cap); else glDisable(cap); }

static void gl_restore(const GlState *s) {
  for (int i = 0; i < T_COUNT; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, s->tex[i]);
    glBindSampler(i, s->samp[i]);
  }
  glActiveTexture(s->active);
  glBindVertexArray(s->vao);
  glUseProgram(s->program);
  glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
  gl_enable(GL_SCISSOR_TEST, s->scissor);
  gl_enable(GL_BLEND, s->blend);
  gl_enable(GL_DEPTH_TEST, s->depth);
  gl_enable(GL_STENCIL_TEST, s->stencil);
  gl_enable(GL_CULL_FACE, s->cull);
  gl_enable(GL_RASTERIZER_DISCARD, s->discard);
  glColorMask(s->mask[0], s->mask[1], s->mask[2], s->mask[3]);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->draw_fb);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, s->read_fb);
}

/* для копий: блит зависит только от ножниц и отбрасывания растеризации */
typedef struct { GLint draw_fb, read_fb; GLboolean scissor, discard; } FbState;

static void fb_save(FbState *s) {
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s->draw_fb);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s->read_fb);
  s->scissor = glIsEnabled(GL_SCISSOR_TEST);
  s->discard = glIsEnabled(GL_RASTERIZER_DISCARD);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_RASTERIZER_DISCARD);
}

static void fb_restore(const FbState *s) {
  gl_enable(GL_SCISSOR_TEST, s->scissor);
  gl_enable(GL_RASTERIZER_DISCARD, s->discard);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->draw_fb);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, s->read_fb);
}

/* ============================================================ ресурсы */
static const char *k_vs =
  "#version 300 es\n"
  "out vec2 v_uv;\n"
  "void main() {\n"
  "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
  "  v_uv = p;\n"
  "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char *k_fs =
  "#version 300 es\n"
  "precision highp float;\n"
  "in vec2 v_uv;\n"
  "out vec4 o;\n"
  "uniform sampler2D u_world, u_gun, u_scene, u_final;\n"
  "uniform mat3 u_h;\n"
  "bool differs(vec3 a, vec3 b) { vec3 d = abs(a - b); return max(d.r, max(d.g, d.b)) > 0.03; }\n"
  "void main() {\n"
  "  vec4 fin = texture(u_final, v_uv);\n"
  "  vec4 sc = texture(u_scene, v_uv);\n"
  "  if (differs(fin.rgb, sc.rgb)) { o = fin; return; }\n"                       /* интерфейс */
  "  if (differs(texture(u_gun, v_uv).rgb, texture(u_world, v_uv).rgb)) { o = sc; return; }\n"   /* оружие */
  "  vec3 s = u_h * vec3(v_uv * 2.0 - 1.0, 1.0);\n"
  "  if (s.z <= 1e-4) { o = sc; return; }\n"
  "  vec2 uv = clamp(s.xy / s.z * 0.5 + 0.5, vec2(0.0), vec2(1.0));\n"
  "  if (differs(texture(u_gun, uv).rgb, texture(u_world, uv).rgb)) { o = sc; return; }\n"     /* мир за оружием не виден */
  "  o = texture(u_scene, uv);\n"
  "}\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = "";
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    debugPrintf("[warp] шейдер не собран: %s\n", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

/* цель снимка: текстура RGBA8 + FBO; вызывать внутри gl_save/gl_restore */
static int make_target(int i, int w, int h) {
  if (g_tex[i]) glDeleteTextures(1, &g_tex[i]);
  if (!g_fbo[i]) glGenFramebuffers(1, &g_fbo[i]);
  glGenTextures(1, &g_tex[i]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_tex[i]);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  const GLint filter = (i == T_WORLD || i == T_GUN) ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo[i]);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex[i], 0);
  const GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  g_tw[i] = w;
  g_th[i] = h;
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    debugPrintf("[warp] FBO снимка %d (%dx%d) неполный: 0x%x\n", i, w, h, st);
    return 0;
  }
  return 1;
}

static void surface_size(int *w, int *h) {
  EGLint ew = 0, eh = 0;
  eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &ew);
  eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &eh);
  *w = ew > 0 ? ew : screen_width;
  *h = eh > 0 ? eh : screen_height;
}

static int init_gl(void) {
  GlState st;
  gl_save(&st);
  int ok = 0;
  GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
  if (vs && fs) {
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (linked) {
      static const char *const names[T_COUNT] = { "u_world", "u_gun", "u_scene", "u_final" };
      for (int i = 0; i < T_COUNT; i++) glProgramUniform1i(g_prog, glGetUniformLocation(g_prog, names[i]), i);
      g_u_h = glGetUniformLocation(g_prog, "u_h");
      glGenVertexArrays(1, &g_vao);
      glGenFramebuffers(1, &g_src_fbo);
      int w, h;
      surface_size(&w, &h);
      ok = make_target(T_SCENE, w, h) && make_target(T_FINAL, w, h);
      debugPrintf("[warp] GL готов: экран %dx%d, программа %u\n", w, h, g_prog);
    } else {
      char log[512] = "";
      glGetProgramInfoLog(g_prog, sizeof log, NULL, log);
      debugPrintf("[warp] программа не связана: %s\n", log);
    }
  }
  if (vs) glDeleteShader(vs);
  if (fs) glDeleteShader(fs);
  gl_restore(&st);
  const GLenum err = glGetError();
  if (err) debugPrintf("[warp] ошибка GL при подготовке: 0x%x\n", err);
  return ok && !err;
}

/* ============================================================ снимки */
static void copy_main(int dst) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, g_src_fbo);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_main_tex, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo[dst]);
  glBlitFramebuffer(0, 0, g_main_w, g_main_h, 0, 0, g_tw[dst], g_th[dst], GL_COLOR_BUFFER_BIT, GL_NEAREST);
  /* чужую текстуру не держим: движок может её удалить */
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
}

static void copy_screen(int dst) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo[dst]);
  glBlitFramebuffer(0, 0, g_tw[dst], g_th[dst], 0, 0, g_tw[dst], g_th[dst], GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

static void warp_event(int id) {
  if (!g_ready) return;
  const uint64_t t0 = armGetSystemTick();
  const int kind = id >> 8;
  FbState fs;
  fb_save(&fs);
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  if (kind == EV_WORLD) {
    g_shown_valid = 0;   /* снимки показанного кадра сейчас перезапишутся */
    g_frame_slot = id & 7;
    if (g_main_tex) { copy_main(T_WORLD); g_have[T_WORLD] = 1; }
  } else if (kind == EV_GUN && fs.draw_fb) {
    GLint type = 0, name = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type == GL_TEXTURE) {
      glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
      if ((GLuint)name != g_main_tex || vp[2] != g_main_w || vp[3] != g_main_h) {
        GlState st;
        gl_save(&st);
        g_main_tex = (GLuint)name;
        g_main_w = vp[2];
        g_main_h = vp[3];
        if (!make_target(T_WORLD, vp[2], vp[3]) || !make_target(T_GUN, vp[2], vp[3])) g_broken = 1;
        gl_restore(&st);
        g_have[T_WORLD] = 0;   /* «мир» этого кадра снят со старой текстуры */
        debugPrintf("[warp] mainTex %u %dx%d\n", g_main_tex, g_main_w, g_main_h);
      }
      copy_main(T_GUN);
      g_have[T_GUN] = 1;
    }
  } else if (kind == EV_SCENE && !fs.draw_fb) {
    copy_screen(T_SCENE);
    g_have[T_SCENE] = 1;
  }
  if (g_trace > 0)
    debugPrintf("[warp] событие %d: fbo %d, область %dx%d\n", kind, fs.draw_fb, vp[2], vp[3]);
  fb_restore(&fs);
  s_capture_ticks += armGetSystemTick() - t0;
}

/* ============================================================ поворот */
static void mul3(const float *a, const float *b, float *o) {
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++)
      o[r * 3 + k] = a[r * 3] * b[k] + a[r * 3 + 1] * b[3 + k] + a[r * 3 + 2] * b[6 + k];
}

static void transpose3(const float *a, float *o) {
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++) o[r * 3 + k] = a[k * 3 + r];
}

static void axis_angle(const float *ax, float a, float *o) {
  const float c = cosf(a), s = sinf(a), t = 1.0f - c, x = ax[0], y = ax[1], z = ax[2];
  const float m[9] = { t * x * x + c, t * x * y - s * z, t * x * z + s * y,
                       t * x * y + s * z, t * y * y + c, t * y * z - s * x,
                       t * x * z - s * y, t * y * z + s * x, t * z * z + c };
  memcpy(o, m, sizeof m);
}

/* гомография для вставки сейчас; 0 -- не вставлять */
static int predict(uint64_t now, float *hc) {
  if (!g_have_prev || g_shown.tick <= g_prev.tick) { s_incomplete++; return 0; }
  float pt[9], d[9];
  transpose3(g_prev.r, pt);
  mul3(g_shown.r, pt, d);                 /* поворот за кадр, в мире */
  const float cs = fminf(1.0f, fmaxf(-1.0f, (d[0] + d[4] + d[8] - 1.0f) * 0.5f));
  const float angle = acosf(cs);
  const float f = fminf(1.5f, (float)((double)(now - g_shown_present) / (double)(g_shown.tick - g_prev.tick)));
  const float a = angle * f;
  if (a < 0.0005f) { s_still++; return 0; }
  if (angle > WARP_MAX_ANGLE || a > WARP_MAX_ANGLE) { s_jump++; return 0; }
  const float sn = sinf(angle);
  float ax[3] = { d[7] - d[5], d[2] - d[6], d[3] - d[1] };
  const float len = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
  if (sn < 1e-6f || len < 1e-9f) { s_still++; return 0; }
  for (int i = 0; i < 3; i++) ax[i] /= len;
  float step[9], rn[9], rt[9], m[9];
  axis_angle(ax, a, step);
  mul3(step, g_shown.r, rn);              /* предсказанный поворот камеры */
  transpose3(g_shown.r, rt);
  mul3(rt, rn, m);                        /* из новой камеры в старую */
  const Pose *p = &g_shown;
  const float k[9] = { p->p00, 0, p->p02, 0, p->p11, p->p12, 0, 0, -1 };
  const float ki[9] = { 1 / p->p00, 0, p->p02 / p->p00, 0, 1 / p->p11, p->p12 / p->p11, 0, 0, -1 };
  float km[9], h[9];
  mul3(k, m, km);
  mul3(km, ki, h);
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) hc[c * 3 + r] = h[r * 3 + c];   /* GLSL: по столбцам */
  return 1;
}

static void insert_frame(uint64_t now) {
  float hc[9];
  if (!predict(now, hc)) {
    g_inserted = WARP_MAX_INSERT;   /* до следующего настоящего кадра предсказание то же */
    return;
  }
  GlState st;
  gl_save(&st);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(0, 0, g_tw[T_SCENE], g_th[T_SCENE]);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_RASTERIZER_DISCARD);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUseProgram(g_prog);
  glProgramUniformMatrix3fv(g_prog, g_u_h, 1, GL_FALSE, hc);
  glBindVertexArray(g_vao);
  for (int i = 0; i < T_COUNT; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, g_tex[i]);
    glBindSampler(i, 0);
  }
  glDrawArrays(GL_TRIANGLES, 0, 3);
  gl_restore(&st);
  g_presenting = 1;
  eglSwapBuffers(g_dpy, g_surf);
  g_presenting = 0;
  g_last_present = armGetSystemTick();
  g_inserted++;
  s_insert++;
  s_insert_ticks += g_last_present - now;
}

/* ============================================================ показ */
int nx_warp_presenting(void) { return g_presenting; }

int nx_warp_on_render_thread(void) { return g_rt_tls && armGetTls() == g_rt_tls; }

void nx_warp_before_swap(void *display, void *surface) {
  if (!g_issue || g_broken) return;
  g_rt_tls = armGetTls();
  g_dpy = (EGLDisplay)display;
  g_surf = (EGLSurface)surface;
  if (!g_ready) {
    g_ready = init_gl();
    if (!g_ready) { g_broken = 1; debugPrintf("[warp] подготовка не удалась -- без перепроецирования\n"); }
    return;
  }
  int w, h;
  surface_size(&w, &h);
  if (w != g_tw[T_SCENE] || h != g_th[T_SCENE]) {   /* док: другой размер экрана */
    GlState st;
    gl_save(&st);
    if (!make_target(T_SCENE, w, h) || !make_target(T_FINAL, w, h)) g_broken = 1;
    gl_restore(&st);
    memset(g_have, 0, sizeof g_have);
    return;
  }
  if (g_have[T_WORLD] && g_have[T_GUN] && g_have[T_SCENE]) {
    const uint64_t t0 = armGetSystemTick();
    FbState fs;
    fb_save(&fs);
    copy_screen(T_FINAL);
    fb_restore(&fs);
    g_have[T_FINAL] = 1;
    s_capture_ticks += armGetSystemTick() - t0;
  }
}

void nx_warp_after_swap(void) {
  if (!g_ready) return;
  const uint64_t now = armGetSystemTick();
  const int complete = g_have[T_WORLD] && g_have[T_GUN] && g_have[T_SCENE] && g_have[T_FINAL] && g_frame_slot >= 0;
  if (complete) {
    /* g_shown_valid к этому месту уже снят событием «мир» этого кадра */
    const Pose p = g_ring[g_frame_slot];
    g_have_prev = g_last_complete && armTicksToNs(p.tick - g_shown.tick) < 100000000ULL;
    g_prev = g_shown;
    g_shown = p;
  } else {
    g_have_prev = 0;
  }
  g_shown_valid = g_last_complete = complete;
  memset(g_have, 0, sizeof g_have);
  g_frame_slot = -1;
  g_shown_present = g_last_present = now;
  g_inserted = 0;
  if (g_trace > 0) g_trace--;
  if (++s_real >= WARP_STATS_FRAMES) {
    debugPrintf("[warp] кадров %u, вставлено %u (%.2f мс), без пары кадров %u, без поворота %u, рывок %u, снимки %.2f мс/кадр\n",
                s_real, s_insert, s_insert ? (double)armTicksToNs(s_insert_ticks) / 1e6 / s_insert : 0.0,
                s_incomplete, s_still, s_jump, (double)armTicksToNs(s_capture_ticks) / 1e6 / s_real);
    s_real = s_insert = s_incomplete = s_still = s_jump = 0;
    s_insert_ticks = s_capture_ticks = 0;
  }
}

int64_t nx_warp_idle(void) {
  if (!g_ready || g_broken || g_presenting || !g_shown_valid || g_inserted >= WARP_MAX_INSERT) return -1;
  const uint64_t now = armGetSystemTick();
  const int64_t since = (int64_t)armTicksToNs(now - g_last_present);
  const int64_t due = WARP_PERIOD_NS - WARP_MARGIN_NS;
  if (since < due) return due - since;
  insert_frame(now);
  return g_inserted >= WARP_MAX_INSERT ? -1 : due;
}
