"""Actual release-based SDL backend: skipped Playing state, backpressure, shutdown."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
 s=(ROOT/'loader/source/nx_sdl_audio.c').read_text(encoding='utf-8');code=s[s.index('static int nx_audio_writable'):s.index('static Uint8\n*NXSDL_GetDeviceBuf')]
 pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef unsigned Result;typedef unsigned char Uint8;typedef int AudioDriverWaveBufState;
enum {AudioDriverWaveBufState_Free,AudioDriverWaveBufState_Waiting,AudioDriverWaveBufState_Queued,AudioDriverWaveBufState_Playing,AudioDriverWaveBufState_Done};
typedef struct {int state;} Wave;
typedef struct {int driver;Wave buffer[2];void *pool,*buffer_tmp;} Hidden;
typedef struct {Hidden *hidden;int shutdown;struct {unsigned size;} spec;} SDL_AudioDevice;
static SDL_AudioDevice *device;static int updates,waits,adds,starts,flushes,disconnected,mode;
static int SDL_AtomicGet(int *x){return *x;}
#define R_FAILED(x) ((x)!=0)
#define debugPrintf(...) ((void)0)
static Result audrvUpdate(int *driver){
 (void)driver;updates++;
 if(mode==1)return 1;
 if(mode==2 && updates==3)device->hidden->buffer[1].state=AudioDriverWaveBufState_Done;
 if(mode==3 && adds)device->hidden->buffer[0].state=AudioDriverWaveBufState_Done;
 return 0;
}
static int* audrenGetFrameEvent(void){static int event;return &event;}
static Result eventWait(int*e,uint64_t timeout){(void)e;assert(timeout==5000000);waits++;return 1;}
static void SDL_OpenedAudioDeviceDisconnected(SDL_AudioDevice*d){assert(d==device);disconnected++;}
static void armDCacheFlush(void*p,unsigned size){(void)p;assert(size==16);flushes++;}
static void audrvVoiceAddWaveBuf(int*d,int voice,Wave*b){(void)d;assert(!voice);assert(b->state==0 || b->state==4);adds++;b->state=AudioDriverWaveBufState_Queued;}
static int audrvVoiceIsPlaying(int*d,int v){(void)d;(void)v;return 0;}
static void audrvVoiceStart(int*d,int v){(void)d;(void)v;starts++;}
'''
 test=r'''
int main(void){
 unsigned char pool[32],pcm[16];memset(pool,0xad,sizeof pool);memset(pcm,0x53,sizeof pcm);
 Hidden hidden={0};hidden.pool=pool;hidden.buffer_tmp=pcm;SDL_AudioDevice dev={&hidden,0,{16}};device=&dev;
 /* Queued -> Done directly, without ever observing Playing. Old backend's
  * while(state != Playing) cannot terminate. New ownership-based path does. */
 mode=3;NXSDL_PlayDevice(&dev);assert(adds==1 && starts==1 && flushes==1 && hidden.buffer[0].state==4);
 NXSDL_WaitDevice(&dev);assert(!waits && !memcmp(pool,pcm,16));
 /* Both buffers owned by renderer: no negative-index access, no overwrite. */
 hidden.buffer[0].state=1;hidden.buffer[1].state=2;mode=0;updates=waits=adds=0;
 NXSDL_PlayDevice(&dev);assert(updates==20 && waits==20 && adds==0);assert(pool[16]==0xad);
 /* Completion between polls releases slot 1 and submits exactly that slot. */
 mode=2;updates=waits=0;NXSDL_PlayDevice(&dev);assert(adds==1 && waits==2 && !memcmp(pool+16,pcm,16));
 dev.shutdown=1;updates=waits=0;NXSDL_WaitDevice(&dev);assert(!updates && !waits);dev.shutdown=0;
 mode=1;updates=waits=adds=0;NXSDL_PlayDevice(&dev);assert(updates==1 && !waits && !adds && disconnected==1);
 return 0;
}
'''
 with tempfile.TemporaryDirectory() as t:
  p=Path(t)/'test.c';exe=Path(t)/'test.exe';p.write_text(pre+code+test)
  subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS: skipped Playing state, bounded full queue, no overwrite, released slot selection, shutdown and IPC error')
if __name__=='__main__':main()
