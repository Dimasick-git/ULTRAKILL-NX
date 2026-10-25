/* Restore the clean-install defaults supplied by the old SD distribution.
 * This file contains local graphics choices, not saves or global preferences.
 * Never replace a nonempty user file. Run before Unity starts reading it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "config.h"
#include "nx_fps_policy.h"
#include "nx_gameroot.h"
#include "util.h"
void nx_preferences_prepare(void) {
  if(!config.performance_mode)return;
  char directory[512],path[512],temporary[544];struct stat st;
  game_path(directory,sizeof directory,"Preferences");mkdir(directory,0777);
  game_path(path,sizeof path,"Preferences/LocalPrefs.json");
  if(stat(path,&st)==0 && st.st_size>0)return;
  snprintf(temporary,sizeof temporary,"%s.nx-defaults",path);
  FILE *f=fopen(temporary,"wb");if(!f)return;
  /* frameRateLimit 3 = 60 FPS, см. nx_preferences_frame_rate */
  const char defaults[]="{\"vSync\":false,\"frameRateLimit\":3,\"maxGore\":64.0}\n";
  int okay=fwrite(defaults,1,sizeof defaults-1,f)==sizeof defaults-1;
  if(fclose(f)!=0)okay=0;
  if(!okay){remove(temporary);debugPrintf("[defaults] FAIL writing initial graphics preferences\n");return;}
  /* Recheck before publication; an existing nonempty file is authoritative. */
  if(stat(path,&st)==0 && st.st_size>0){remove(temporary);return;}
  int result=rename(temporary,path);
  if(result!=0 && stat(path,&st)==0 && st.st_size==0) {
    if(remove(path)==0)result=rename(temporary,path);
  }
  if(result!=0){remove(temporary);debugPrintf("[defaults] FAIL publishing initial graphics preferences\n");}
  else debugPrintf("[defaults] initial local profile: vSync=off frameRateLimit=3 (60) maxGore=64 (empty/missing file only)\n");
}

/* Preserve explicit saved caps; a missing preference defaults to 60 FPS. */
int nx_preferences_frame_rate(void) {
  static const int rates[] = { 1000, 120, 30, 60, 120, 144, 240, 288 };
  char path[512], text[2048];
  game_path(path, sizeof path, "Preferences/LocalPrefs.json");
  FILE *f = fopen(path, "rb");
  if (!f) return 60;
  const size_t n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n] = 0;
  const char *key = strstr(text, "\"frameRateLimit\"");
  if (!key) return 60;
  key = strchr(key + 16, ':');
  if (!key) return 60;
  const int index = atoi(key + 1);
  return nx_fps_resolve(index >= 0 && index < (int)(sizeof rates / sizeof rates[0]) ? rates[index] : 60, 0, 0);
}
