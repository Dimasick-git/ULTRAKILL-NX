"""Exercise production frame-report code and render-hook install gating."""
from pathlib import Path
import argparse,subprocess,tempfile

def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--cc-arg',action='append',default=[]);a=p.parse_args()
 r=Path(__file__).resolve().parents[1]
 frame=(r/'loader/source/nx_frame.c').read_text(encoding='utf-8');report=frame[frame.index('static void report('):frame.index('void nx_frame_loop_begin')]
 imports=(r/'loader/source/imports.c').read_text(encoding='utf-8');cache=imports[imports.index('static void nx_shader_cache_report('):imports.index('static EGLBoolean egl_SwapBuffers_log(')]
 hook=(r/'loader/source/nx_hook.c').read_text(encoding='utf-8');start=hook.index('void nx_hook_install(');prefix=hook[start:hook.index('  const unsigned total =',start)]
 pre=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
typedef int GLint;typedef unsigned char GLubyte;
#define GL_RENDERER 0
#define GL_VERSION 1
static int cache_scans,format_queries;
static void glGetIntegerv(int key,GLint *out){(void)key;++format_queries;*out=1;}
static const GLubyte* glGetString(int key){(void)key;return (const GLubyte*)"fixture";}
static const char* game_root(void){return "/unused";}
static DIR* counted_opendir(const char *path){(void)path;++cache_scans;return NULL;}
#define opendir counted_opendir
#define NX_FRAME_SLOTS 4
#define NX_FRAME_OUTSIDE 4
#define NX_FRAME_TOP 12
static struct {int engine_profiler,frame_stats,reproject;} config;
static char output[32000];static int length,hook_reports,installed;
static int debugPrintf(const char *fmt,...){va_list a;va_start(a,fmt);int n=vsnprintf(output+length,sizeof(output)-length,fmt,a);va_end(a);length+=n;return n;}
static uint64_t armTicksToNs(uint64_t t){return t;}
static double ms(uint64_t t){return (double)t/1e6;}
static uint64_t w_frames=60,t_window=1,w_pre=60000000,w_render=900000000;
static int g_physics=1;static unsigned g_used=2;
static const char *g_name[]={"Update.ScriptRunBehaviourUpdate","FixedUpdate.PhysicsFixedUpdate"};
static uint64_t g_ticks[4]={200000000,100000000},g_calls[4]={60,120},g_max[4],g_wait[5];
static uint64_t g_fence_ticks,g_fence_calls,g_dequeue_ticks,g_gc_ticks,g_gc_count,g_gc_max;
static uint64_t g_present_frames,g_present_free,g_present_switches;
static void nx_hook_report(double frames){assert(frames==60);++hook_reports;}
"""
 tests=r"""
int main(void){
 config.frame_stats=1;nx_hook_install(0,0);assert(!installed);
 g_gc_count=2;g_gc_ticks=8000000;g_present_frames=60;
 report(1000000001);assert(strstr(output,"[frame]") && strstr(output,"[frame-wait]"));
 assert(!strstr(output,"[frame-phase]") && !strstr(output,"ScriptRunBehaviourUpdate") && !hook_reports);
 assert(!g_gc_count && !g_gc_ticks && !g_present_frames);
 int lines=0;for(int i=0;i<length;i++)if(output[i]=='\n')++lines;assert(lines==2);
 length=0;output[0]=0;config.engine_profiler=1;nx_hook_install(0,0);assert(installed==1);
 report(1000000001);assert(strstr(output,"[frame-phase]") && strstr(output,"ScriptRunBehaviourUpdate") && hook_reports==1);
 config.frame_stats=0;nx_hook_install(0,0);assert(installed==1);
 config.engine_profiler=0;config.reproject=1;nx_hook_install(0,0);assert(installed==2);
 nx_shader_cache_report(1);assert(format_queries==1);
 nx_shader_cache_report(600);nx_shader_cache_report(3600);assert(cache_scans==0);
 config.engine_profiler=1;nx_shader_cache_report(3600);assert(cache_scans==1);
 return 0;
}
"""
 with tempfile.TemporaryDirectory() as tmp:
  c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(pre+prefix+'  (void)ub;(void)usize;++installed;\n}\n'+report+cache+tests,encoding='utf-8')
  subprocess.run([a.cc,*a.cc_arg,'-std=c11','-O2','-UNDEBUG',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS: normal mode installs no detailed detours and emits two frame-report lines; counters drain; opt-in details/capture retained; no diagnostic SD cache scan in normal play')
if __name__=='__main__':main()
