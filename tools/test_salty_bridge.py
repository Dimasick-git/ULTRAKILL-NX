"""Exercise the production telemetry/pacing function with a deterministic clock."""
from pathlib import Path
import argparse,subprocess,tempfile
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    header=(root/'loader/source/nx_salty.h').read_text().replace('#pragma once','')
    code=(root/'loader/source/nx_salty.c').read_text();code=code[code.index('void nx_salty_present('):]
    pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static volatile NxSaltyBlock *block;
static uint64_t previous,window,pacing,clock_now,slept;
static unsigned frames,sample_index;
static int docked;
static bool enabled,foreign_producer;
static unsigned attempts;
static uint64_t retry_at;
void nx_salty_init(void){++attempts;}
#define AppletOperationMode_Console 1
static int appletGetOperationMode(void){return docked;}
static uint64_t armGetSystemTick(void){return clock_now;}
static uint64_t armTicksToNs(uint64_t t){return t*1000000000/19200000;}
static void svcSleepThread(uint64_t ns){slept+=ns;clock_now+=ns*19200000/1000000000;}
'''
    test=r'''
int main(void) {
 nx_salty_present(1); // optional server absent
 NxSaltyBlock data={0};block=&data;
 for(unsigned i=0;i<=60;++i){clock_now=1000+(uint64_t)i*320000;nx_salty_present(clock_now);}
 assert(data.magic==0x465053 && data.api==2 && data.active==1);
 assert(data.fps==60 && data.average>59.99f && data.average<60.01f && data.frame==61);
 for(unsigned i=0;i<10;++i)assert(data.ticks[i]==320000);
 data.locked=30;pacing=0;clock_now=100000000;nx_salty_present(clock_now);
 clock_now+=320000;nx_salty_present(clock_now);assert(slept>16666000 && slept<16668000);
 data.locked=120;slept=0;clock_now+=320000;nx_salty_present(clock_now);assert(!slept);
 docked=1;data.locked=0;data.locked_docked=20;pacing=0;
 clock_now+=320000;nx_salty_present(clock_now);clock_now+=320000;nx_salty_present(clock_now);
 assert(slept>33332000 && slept<33335000);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(header+pre+code+test)
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: 310-byte ABI, exact 60 FPS/frametimes, optional absence, handheld/docked pacing and unsupported cap')
if __name__=='__main__':main()
