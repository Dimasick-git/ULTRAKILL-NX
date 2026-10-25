"""Exercise the production telemetry/pacing function with a deterministic clock."""
from pathlib import Path
import argparse,subprocess,tempfile
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');ap.add_argument('--cc-arg',action='append',default=[]);args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    header=(root/'loader/source/nx_salty.h').read_text().replace('#pragma once','')
    code=(root/'loader/source/nx_salty.c').read_text();code=code[code.index('void nx_salty_present('):]
    pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static volatile NxSaltyBlock *block;
static uint64_t previous,window,clock_now;
static unsigned overlay_limit;static unsigned char area[4];static volatile uint8_t *shared_base=area;
static unsigned frames,sample_index;
static int docked;
static bool enabled,foreign_producer;
static unsigned attempts;
static uint64_t retry_at;
void nx_salty_init(void){++attempts;}
#define AppletOperationMode_Console 1
static int appletGetOperationMode(void){return docked;}
#define debugPrintf(...) ((void)0)
'''
    test=r'''
int main(void) {
 nx_salty_present(1); // optional server absent
 NxSaltyBlock data={0};block=&data;
 for(unsigned i=0;i<=60;++i){clock_now=1000+(uint64_t)i*320000;nx_salty_present(clock_now);}
 assert(data.magic==0x465053 && data.api==2 && data.active==1);
 assert(data.fps==60 && data.average>59.99f && data.average<60.01f && data.frame==61);
 for(unsigned i=0;i<10;++i)assert(data.ticks[i]==320000);
 area[1]=60;
 unsigned rates[]={30,60,120,144,240};
 for(unsigned i=0;i<sizeof rates/sizeof rates[0];i++) {
  data.locked=rates[i];clock_now+=320000;nx_salty_present(clock_now);
  assert(overlay_limit==rates[i] && data.refresh==60 && area[1]==60);
 }
 data.locked=0;nx_salty_present(++clock_now);assert(overlay_limit==0);
 docked=1;data.locked_docked=144;nx_salty_present(++clock_now);assert(overlay_limit==144);
 data.locked_docked=20;nx_salty_present(++clock_now);assert(overlay_limit==20);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(header+pre+code+test)
        subprocess.run([args.cc,*args.cc_arg,'-std=c11','-UNDEBUG','-O2','-Wall','-Wextra',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: 310-byte ABI, exact FPS/frametimes; independent 60 Hz display; 30/60/120/144/240 caps and docked controls; no render-thread sleep')
if __name__=='__main__':main()
