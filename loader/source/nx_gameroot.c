/* nx_gameroot.c -- resolve the game folder at RUNTIME instead of baking it in.
 *
 * A hardcoded data root means the .nro only works from one exact folder name.
 * That is a bad deal for the user: everyone has their own naming habit
 * (ultrakill, ULTRAKILL, ultrakill-nx), and picking the wrong one produces a
 * silent failure that looks like a broken build rather than a misplaced folder.
 *
 * So: work out where we actually are, and use that.
 *
 * RESOLUTION ORDER
 *   1. argv[0]. hbmenu passes the full path of the .nro it launched, e.g.
 *      "sdmc:/switch/ultrakill/ultrakill_nx.nro". Take its directory.
 *      This is the normal case and it is exact.
 *
 *   2. Scan sdmc:/switch/<*> for a directory that contains libil2cpp.so.
 *      This covers the launch paths where argv is not useful -- title override
 *      (holding R on an installed game) can hand us an empty or synthetic argv,
 *      and some forwarders do the same. Looking for the game's own library is a
 *      far better signal than guessing a folder name, and it means a user who
 *      renamed their folder still boots.
 *
 *   3. The compile-time GAME_HOME default, so behaviour never gets *worse* than
 *      the hardcoded version if both of the above fail.
 *
 * Everything is resolved once, at the top of main(), into static storage. After
 * that game_root() is a plain pointer read and is safe to call from any thread.
 *
 * MIT, same as the rest of the tree.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "nx_gameroot.h"

/* "sdmc:/switch/<folder>"  -- device-qualified, no trailing slash */
static char g_root[256];
/* "/switch/<folder>"       -- Unix-rooted, for managed Path APIs */
static char g_root_unix[256];
/* "sdmc:/switch/<folder>/debug.log" */
static char g_log[288];
static char g_apk[272];
static int  g_resolved = 0;
static int g_installed;

/* The marker we look for in step 2. libil2cpp.so is the right choice: it is
 * unambiguously this game's, it is always present (unlike an OBB or assets
 * layout that varies), and it is what the loader is going to open anyway. */
#define ROOT_MARKER "libil2cpp.so"

static int dir_has_marker(const char *dir) {
  char probe[512];
  if (snprintf(probe, sizeof probe, "%s/" ROOT_MARKER, dir) <= 0) return 0;
  struct stat st;
  return stat(probe, &st) == 0;
}

/* Strip the trailing "/<file>" from a path, in place. Returns 0 if there was no
 * separator to strip (i.e. argv[0] was a bare filename, which tells us nothing). */
static int strip_filename(char *p) {
  char *slash = strrchr(p, '/');
  char *bslash = strrchr(p, '\\');          /* be tolerant of either separator */
  if (bslash && (!slash || bslash > slash)) slash = bslash;
  if (!slash) return 0;
  if (slash == p) { p[1] = '\0'; return 1; } /* root: keep the "/" */
  *slash = '\0';
  return 1;
}

/* Fill g_root_unix and g_log from g_root. */
static void derive_from_root(void) {
  /* Unix-rooted form: drop a leading "device:" if present. Managed Path.Combine
   * treats "sdmc:/x" as a relative path on a drive, which silently corrupts
   * every path it builds, so the engine must be handed "/switch/...". */
  const char *colon = strchr(g_root, ':');
  const char *unix_part = (colon && colon[1] == '/') ? colon + 1 : g_root;
  snprintf(g_root_unix, sizeof g_root_unix, "%s", unix_part);

  snprintf(g_log, sizeof g_log, "%s/debug.log", g_root);
  snprintf(g_apk, sizeof g_apk, "%s/base.apk", g_root_unix);
}

/* Step 2: look for the folder that actually holds the game. */
static int scan_switch_dir(void) {
  DIR *d = opendir("sdmc:/switch");
  if (!d) return 0;
  struct dirent *de;
  int found = 0;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue;
    char cand[256];
    if (snprintf(cand, sizeof cand, "sdmc:/switch/%s", de->d_name) <= 0) continue;
    struct stat st;
    if (stat(cand, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    if (dir_has_marker(cand)) {
      snprintf(g_root, sizeof g_root, "%s", cand);
      found = 1;
      break;
    }
  }
  closedir(d);
  return found;
}

void game_root_init(int argc, char **argv) {
  if (g_resolved) return;
  g_resolved = 1;

  /* The full installer launches its own NRO, never a stale SD copy. Mount the
   * host NSO's content explicitly: romfsInit() would inspect the NRO instead. */
  if (argc>0 && argv && argv[0] && !strncmp(argv[0],"romfs:/ultrakill/",17)) {
    Result rc=romfsMountFromCurrentProcess("nxdata");
    struct stat st;
    if (R_FAILED(rc) || stat("nxdata:/ultrakill/offsets.ini",&st)!=0)
      diagAbortWithResult(MAKERESULT(Module_HomebrewLoader,405));
    g_installed=1;
    snprintf(g_root,sizeof g_root,"%s",GAME_HOME);
    mkdir("sdmc:/switch",0777);mkdir(g_root,0777);
    derive_from_root();return;
  }

  /* --- 1. argv[0] --------------------------------------------------------- */
  if (argc > 0 && argv && argv[0] && argv[0][0]) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", argv[0]);
    if (strip_filename(tmp) && tmp[0]) {
      /* Only trust it if it looks like a real place on the SD card. A path with
       * no device prefix and no leading slash is not something we can chdir to. */
      if (strchr(tmp, ':') || tmp[0] == '/') {
        snprintf(g_root, sizeof g_root, "%s", tmp);
        /* Trailing slash tidy-up, so callers can always append "/name". */
        size_t n = strlen(g_root);
        while (n > 1 && g_root[n - 1] == '/') g_root[--n] = '\0';
        if (dir_has_marker(g_root)) { derive_from_root(); return; }
        /* argv[0] pointed somewhere real but the game files are not there.
         * Keep looking rather than failing: a forwarder may have launched a
         * copy of the .nro from elsewhere. */
      }
    }
  }

  /* --- 2. scan sdmc:/switch ---------------------------------------------- */
  if (scan_switch_dir()) { derive_from_root(); return; }

  /* --- 3. compile-time default ------------------------------------------- */
  snprintf(g_root, sizeof g_root, "%s", GAME_HOME);
  derive_from_root();
}

/* Lazy fallback so a caller that runs before game_root_init() -- an early
 * debugPrintf, say -- still gets something usable rather than an empty string. */
static void ensure(void) {
  if (!g_resolved) game_root_init(0, NULL);
}

const char *game_root(void)      { ensure(); return g_root; }
const char *game_root_unix(void) { ensure(); return g_root_unix; }
const char *game_log_path(void)  { ensure(); return g_log; }
const char *game_apk_path(void)  { ensure(); return g_apk; }

int game_path(char *buf, size_t sz, const char *rel) {
  ensure();
  if (!buf || sz == 0) return -1;
  if (!rel || !*rel) return snprintf(buf, sz, "%s", g_root);
  while (*rel == '/') rel++;               /* accept "/x" or "x" alike */
  return snprintf(buf, sz, "%s/%s", g_installed && game_content_relative(rel) ? "nxdata:/ultrakill" : g_root, rel);
}

int game_installed(void) { ensure();return g_installed; }

int game_content_relative(const char *rel) {
  if (!rel || strstr(rel,"..") || strchr(rel,'\\')) return 0;
  return !strcmp(rel,"assets") || !strncmp(rel,"assets/",7) ||
    !strcmp(rel,"licenses") || !strncmp(rel,"licenses/",9) ||
    !strcmp(rel,"libunity.so") || !strcmp(rel,"libmain.so") ||
    !strcmp(rel,"libil2cpp.so") || !strcmp(rel,"offsets.ini");
}

const char *game_content_redirect(const char *path,char *buf,size_t sz) {
  if (!game_installed() || !path) return path;
  const char *rel=path;
  size_t n=strlen(g_root),u=strlen(g_root_unix);
  if (!strncmp(rel,g_root,n) && rel[n]=='/') rel+=n+1;
  else if (!strncmp(rel,g_root_unix,u) && rel[u]=='/') rel+=u+1;
  else if (*rel=='/' || strchr(rel,':')) return path;
  if (!game_content_relative(rel)) return path;
  snprintf(buf,sz,"nxdata:/ultrakill/%s",rel);return buf;
}
