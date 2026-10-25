/* nx_offsets.c -- offsets.ini: адреса НАШЕЙ сборки libil2cpp/libunity. См. nx_offsets.h.
 *
 * Строка -- «ключ = число [число [число]]». Одно число -- смещение
 * (il2cpp.*, unity.<native>); три -- заплатка кода «адрес было стало»
 * (patch.unity.*); два -- запись байта в данные «адрес значение» (poke8.unity.*). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nx_offsets.h"
#include "nx_gameroot.h"
#include "util.h"

#define NXO_MAX 160   /* в offsets.ini около 90 записей */

static struct { char key[64]; uint32_t v[3]; int n; } g_tab[NXO_MAX];
static int g_n = 0;
static int g_loaded = 0;

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

void nxo_load(void) {
  if (g_loaded) return;
  g_loaded = 1;

  char path[320];
  game_path(path, sizeof path, "offsets.ini");
  FILE *f = fopen(path, "r");
  if (!f) {
    debugPrintf("[offsets] %s нет: заплатки по смещениям выключены\n", path);
    return;
  }

  char line[256];
  while (fgets(line, sizeof line, f) && g_n < NXO_MAX) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';') continue;
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = trim(s), *v = trim(eq + 1);
    if (!*k || strlen(k) >= sizeof g_tab[0].key) continue;
    snprintf(g_tab[g_n].key, sizeof g_tab[g_n].key, "%s", k);
    int n = 0;
    while (*v && n < 3) {
      char *end;
      g_tab[g_n].v[n] = (uint32_t)strtoul(v, &end, 0);
      if (end == v) break;
      n++;
      v = end;
      while (*v && isspace((unsigned char)*v)) v++;
    }
    g_tab[g_n].n = n;
    debugPrintf("[offsets] %s = 0x%x%s\n", g_tab[g_n].key, g_tab[g_n].v[0], n > 1 ? " ..." : "");
    g_n++;
  }
  fclose(f);
}

uint32_t nxo_get(const char *key) {
  for (int i = 0; i < g_n; i++)
    if (!strcmp(g_tab[i].key, key)) return g_tab[i].v[0];
  return 0;
}

int nxo_each(const char *prefix, int nvals, nxo_cb cb, void *ctx) {
  const size_t pl = strlen(prefix);
  int done = 0;
  for (int i = 0; i < g_n; i++) {
    if (strncmp(g_tab[i].key, prefix, pl) || g_tab[i].n != nvals) continue;
    cb(ctx, g_tab[i].key, g_tab[i].v);
    done++;
  }
  return done;
}
