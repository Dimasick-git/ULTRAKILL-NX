/* movie_player.h -- FMV interface of the base loader (cr3_nx), referenced by
 * jni_fake.c's unused MoviePlayer handler. The bodies are no-ops in
 * cr3_stubs.c; ULTRAKILL's videos go through nx_video.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __MOVIE_PLAYER_H__
#define __MOVIE_PLAYER_H__

// resolve the engine's Fios reader at boot (before its symbol table is freed)
void movie_player_init(void);
void movie_set_db(const char *db_path);
int  movie_play(const char *name, int looping); // returns 1 if a movie played
void movie_stop(void);
void movie_pause(void);
void movie_resume(void);
int  movie_is_playing(void);

#endif
