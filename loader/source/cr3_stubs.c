/* cr3_stubs.c -- no-op bodies for the helpers that jni_fake.c's fallback
 * handlers (t2b, mov, act) still reference. They come from the base loader
 * (cr3_nx, a Chaos Rings 3 port).
 *
 * In this port those handlers never run: jni_fake.c delegates every Unity/input
 * class to unity_jni.c / unity_input.c before reaching them. They only need to
 * link, so everything here returns a benign default instead of pulling in the
 * base loader's FreeType and FFmpeg code.
 *
 * If a stub ever actually fires at runtime, a class slipped past the delegation
 * in unity_owns_class() / input_owns_class() -- add it there.
 */

#include <stddef.h>
#include "data.h"
#include "text2bitmap.h"
#include "movie_player.h"
#include "config.h"
#include "nx_gameroot.h"
#include "editbox.h"

/* ---- data layer ---- */
/* The il2cpp runtime uses this as its persistentDataPath: it extracts and mmaps
 * unity.ver / global-metadata.dat / Resources/ underneath it, and the game's
 * saves live there too, so it must be this game's folder, never a fixed path. */
const char *data_dir(void) { return game_root(); }

/* Thread bookkeeping: imports.c's thread_trampoline calls this when the
 * first engine thread returns. Our main loop uses jni_quit_requested instead, so
 * this is a no-op. */
void android_mark_main_finished(void) {}

/* ---- text rendering (engine's dynamic text path; Unity doesn't use it) ---- */
FakeBitmap *text2bitmap_render(const char *text, int pixel_size) { (void)text; (void)pixel_size; return NULL; }
int  text2bitmap_measure_width (const char *text, int pixel_size) { (void)text; (void)pixel_size; return 0; }
int  text2bitmap_measure_height(const char *text, int pixel_size) { (void)text; (void)pixel_size; return 0; }
void text2bitmap_free(FakeBitmap *bmp) { (void)bmp; }

/* ---- FMV playback (unused) ---- */
void movie_set_db(const char *db_path) { (void)db_path; }
int  movie_play(const char *name, int looping) { (void)name; (void)looping; return 0; }
void movie_stop(void)   {}
void movie_pause(void)  {}
void movie_resume(void) {}
int  movie_is_playing(void) { return 0; }

/* ---- software keyboard (Unity routes through SoftInputProvider stub) ---- */
void editbox_show(const char *initial, int maxlen) { (void)initial; (void)maxlen; }
int  editbox_is_open(void) { return 0; }
const char *editbox_text(void) { return ""; }
void editbox_close(void) {}
