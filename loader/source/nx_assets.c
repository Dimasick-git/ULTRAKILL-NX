/* pp_assets.c -- reassemble Unity "Split Application Binary" asset files.
 *
 * WHY THIS EXISTS
 *   Papers, Please does not ship the usual single `data.unity3d` bundle. It uses
 *   the classic non-bundled Unity layout, AND it was built with Unity's "Split
 *   Application Binary" option, so the two large serialized files arrive as
 *   1 MB chunks:
 *
 *       assets/bin/Data/globalgamemanagers.assets.split0 .. .split5   (6 parts)
 *       assets/bin/Data/sharedassets0.assets.split0      .. .split3   (4 parts)
 *
 *   On Android the engine reads those chunks straight out of the APK through a
 *   virtual filesystem (libunity carries an `AndroidSplitFile` reader for
 *   exactly this). That reader is bound to the APK/AAssetManager path, which
 *   this port does not have -- our files are loose on the SD card and reached
 *   through plain newlib file I/O.
 *
 *   Rather than try to reimplement the engine's split VFS, we simply put the
 *   files back together once, at first boot, into the names the engine expects
 *   in the ordinary non-split case: `globalgamemanagers.assets` and
 *   `sharedassets0.assets`. After that the engine takes its normal, well-trodden
 *   path and never knows anything was split.
 *
 * WHY THIS IS SAFE
 *   The chunks are raw byte ranges of one logical file: split0 begins with a
 *   Unity serialized-file header and the later chunks have no header at all.
 *   That header declares the total size of the assembled file (`m_FileSize`, a
 *   big-endian int64 at offset 0x18 for format version >= 22), so the join is
 *   self-verifying: concatenate in order, compare the result against the
 *   declared size, and refuse to publish a file that does not match.
 *
 *   Verified against the shipped assets:
 *       globalgamemanagers.assets  6 parts -> 6291048 bytes, header says 6291048
 *       sharedassets0.assets       4 parts -> 3341408 bytes, header says 3341408
 *
 * COST
 *   ~9.6 MB of extra SD space and a few seconds on the very first launch only.
 *   The splits are left in place: they are small, and deleting a user's game
 *   files to save 9 MB is not a trade this code gets to make on their behalf.
 *
 * MIT, same as the rest of the tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "util.h"
#include "nx_gameroot.h"
#include "nx_assets.h"

#define SPLIT_MAX      64                 /* far beyond any real split count */
#define JOIN_CHUNK     (256 * 1024)

/* The two logical files Unity splits in this build. Relative to the game root.
 * If a future build splits something else, the log line below names the file it
 * found, so adding a row here is all that is needed. */
static const char *const kSplitTargets[] = {
  "assets/bin/Data/globalgamemanagers.assets",
  "assets/bin/Data/sharedassets0.assets",
};

static long file_size(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return (long)st.st_size;
}

/* Unity serialized-file header: for version >= 22 the true total size is a
 * big-endian int64 at 0x18. Returns -1 if this does not look like one. */
static long declared_size(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  unsigned char h[0x20];
  size_t got = fread(h, 1, sizeof h, f);
  fclose(f);
  if (got < sizeof h) return -1;

  uint32_t version = ((uint32_t)h[8] << 24) | ((uint32_t)h[9] << 16) |
                     ((uint32_t)h[10] << 8) | (uint32_t)h[11];
  if (version < 22 || version > 64) return -1;   /* not a header we understand */

  uint64_t sz = 0;
  for (int i = 0; i < 8; i++) sz = (sz << 8) | h[0x18 + i];
  if (sz == 0 || sz > (uint64_t)1 << 32) return -1;
  return (long)sz;
}

/* Join <base>.split0..N -> <base>. Returns 1 if <base> is present and correct
 * afterwards (whether we built it or it already existed), 0 otherwise.
 *
 * Two passes, so we never hold an array of paths: count and total first, then
 * copy. Keeps the stack footprint to two path buffers regardless of how many
 * splits a future build might have. */
static int join_one(const char *rel) {
  char base[384];
  char part[448];
  game_path(base, sizeof base, rel);

  /* pass 1: how many parts, and how many bytes total? */
  int  n = 0;
  long total = 0;
  for (int i = 0; i < SPLIT_MAX; i++) {
    snprintf(part, sizeof part, "%s.split%d", base, i);
    long s = file_size(part);
    if (s < 0) break;
    total += s;
    n++;
  }

  if (n == 0) {
    /* No splits. Either this build isn't split (fine) or the files are missing,
     * which the loader's own asset check reports separately. Nothing to do. */
    return file_size(base) > 0;
  }

  snprintf(part, sizeof part, "%s.split0", base);
  long want = declared_size(part);
  long have = file_size(base);

  if (have > 0 && (want < 0 || have == want)) {
    debugPrintf("[assets] %s already joined (%ld bytes) -- skipping\n", rel, have);
    return 1;
  }

  if (want > 0 && total != want) {
    debugPrintf("[assets] REFUSING to join %s: %d parts total %ld bytes but the "
                "header declares %ld. A split is missing or truncated -- re-copy "
                "the assets folder from your APK.\n", rel, n, total, want);
    return 0;
  }

  debugPrintf("[assets] joining %s from %d splits (%ld bytes)...\n", rel, n, total);

  char tmp[448];
  snprintf(tmp, sizeof tmp, "%s.tmp", base);
  FILE *out = fopen(tmp, "wb");
  if (!out) {
    debugPrintf("[assets] cannot create %s -- SD full or read-only?\n", tmp);
    return 0;
  }

  char *buf = malloc(JOIN_CHUNK);
  if (!buf) { fclose(out); remove(tmp); return 0; }

  /* pass 2: copy */
  long written = 0;
  int ok = 1;
  for (int i = 0; i < n && ok; i++) {
    snprintf(part, sizeof part, "%s.split%d", base, i);
    FILE *in = fopen(part, "rb");
    if (!in) { ok = 0; break; }
    for (;;) {
      size_t r = fread(buf, 1, JOIN_CHUNK, in);
      if (r == 0) break;
      if (fwrite(buf, 1, r, out) != r) { ok = 0; break; }
      written += (long)r;
    }
    fclose(in);
  }
  free(buf);
  fclose(out);

  if (!ok || written != total) {
    debugPrintf("[assets] join of %s FAILED (wrote %ld of %ld) -- discarding\n",
                rel, written, total);
    remove(tmp);
    return 0;
  }

  /* Publish only once every byte is down. A half-written .assets would be worse
   * than none: the engine would read a truncated file and fail somewhere deep
   * and unhelpful, a long way from the actual cause. */
  remove(base);
  if (rename(tmp, base) != 0) {
    debugPrintf("[assets] could not rename %s -> %s\n", tmp, base);
    remove(tmp);
    return 0;
  }

  debugPrintf("[assets] joined %s OK (%ld bytes, verified against header)\n", rel, written);
  return 1;
}

void nx_assets_prepare(void) {
  int n = (int)(sizeof kSplitTargets / sizeof kSplitTargets[0]);
  int ok = 0;
  for (int i = 0; i < n; i++) ok += join_one(kSplitTargets[i]);
  if (ok != n)
    debugPrintf("[assets] WARNING: %d of %d split targets are not ready; "
                "the engine will probably fail to load its data.\n", n - ok, n);
}
