"""Exercise actual OpenSL mixer volume/mute callbacks and JNI audio answers."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
 src=(ROOT/'loader/source/opensles.c').read_text()
 declarations=src[src.index('// --- OpenSL ES constants'):src.index('// --- global SDL device')]
 mixer=src[src.index('static float mb_to_linear'):src.index('static void mix_movie')]
 volume=src[src.index('static SLresult vol_SetVolumeLevel'):src.index('// --- playback rate interface')]
 jni=(ROOT/'loader/source/jni_fake.c').read_text()
 audio=jni[jni.index('  /* SDL/Horizon owns hardware volume'):jni.index('  // java.lang.Integer.parseInt')]
 pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
typedef int SDL_mutex;
static int SDL_LockMutex(SDL_mutex *m){assert(m && !*m);*m=1;return 0;}
static int SDL_UnlockMutex(SDL_mutex *m){assert(*m);*m=0;return 0;}
#define debugPrintf(...) ((void)0)
'''
 test=r'''
static int completions;
static void completed(void *self,void *ctx){(void)self;(void)ctx;completions++;}
static void render(Player *p,int expected){
 static int16_t pcm[]={10000,10000,10000,10000};
 p->cur=NULL;p->cur_fpos=0;p->q_head=0;p->q_tail=1;p->q[0].data=pcm;p->q[0].size=sizeof pcm;
 int32_t out[6]={0};mix_player(p,out,3);
 assert(abs(out[0]-expected)<=1 && out[0]==out[1]);
 assert(out[4]==0 && completions>0); /* completion still fires when muted */
}
int main(void){
 assert(sizeof(SLmillibel)==2 && SL_RESULT_PARAMETER_INVALID==2);
 SDL_mutex lock=0;Player p={0};p.lock=&lock;p.channels=2;p.rate=48000;p.sbytes=2;p.playing=1;p.gain=1;p.cb=completed;
 struct {uint16_t before;SLmillibel value;uint16_t after;} guard={0x1234,77,0x5678};
 vol_vtable.GetVolumeLevel(&p.vol_vt,&guard.value);
 assert(guard.before==0x1234 && guard.value==0 && guard.after==0x5678);
 vol_vtable.GetMaxVolumeLevel(&p.vol_vt,&guard.value);
 assert(guard.before==0x1234 && guard.after==0x5678);
 SLmillibel mb=77;SLboolean muted=77;
 render(&p,10000);vol_SetVolumeLevel(&p.vol_vt,-600);vol_GetVolumeLevel(&p.vol_vt,&mb);assert(mb==-600);
 render(&p,5011);vol_SetMute(&p.vol_vt,1);vol_GetMute(&p.vol_vt,&muted);assert(muted==1);render(&p,0);
 vol_SetVolumeLevel(&p.vol_vt,-1200);render(&p,0);
 vol_SetMute(&p.vol_vt,0);vol_GetMute(&p.vol_vt,&muted);assert(!muted);render(&p,2511);
 for(int i=0;i<100;i++){vol_SetMute(&p.vol_vt,1);vol_SetMute(&p.vol_vt,0);}render(&p,2511);
 vol_SetVolumeLevel(&p.vol_vt,-9600);render(&p,0);vol_SetMute(&p.vol_vt,1);vol_SetMute(&p.vol_vt,0);render(&p,0);
 vol_SetVolumeLevel(&p.vol_vt,300);render(&p,10000);
 assert(vol_GetMute(&p.vol_vt,NULL)==SL_RESULT_PARAMETER_INVALID);
 assert(vol_GetVolumeLevel(&p.vol_vt,NULL)==SL_RESULT_PARAMETER_INVALID);
 assert(audio_answer("android/media/AudioManager","getStreamVolume")==15);
 assert(audio_answer("android/media/AudioManager","getStreamMaxVolume")==15);
 assert(audio_answer("android/media/AudioManager","requestAudioFocus")==1);
 assert(audio_answer("android/media/AudioManager","isStreamMute")==0);
 assert(audio_answer("other/Class","getStreamVolume")==-1);
 return 0;
}
'''
 helper='typedef struct {const char *cls,*name;} FakeID;\nstatic int name_has(const char *s,const char *n){return strstr(s,n)!=NULL;}\nstatic int audio_answer(const char *cls,const char *name){FakeID value={cls,name};const FakeID *id=&value;\n'+audio+'return -1;}\n'
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';p.write_text(pre+declarations+'\nstatic int g_dev_rate=48000;\n'+mixer+volume+helper+test)
  subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra',str(p),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS: actual mixer mute/unmute, preserved volume, muted queue completion, Android volume/focus answers')
if __name__=='__main__':main()
