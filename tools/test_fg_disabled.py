"""Exercise the production no-FG ABI with a host compiler; no GPU stubs hidden
behind alternate implementations. The built Switch ELF is audited separately.
"""
from pathlib import Path
import argparse,subprocess
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--includes',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
 source=a.output/'fg_disabled_test.c';source.write_text('''#include "nx_fg.h"
#include "nx_fg_runtime.h"
#include <assert.h>
int main(void){NxFgDescription d={0};NxFgFrame f={0};assert(!nxb_fg_available());assert(!nxb_fg_create(&d));assert(!nxb_fg_create(0));assert(!nxb_fg_enqueue(0,&f));assert(!nxb_fg_callback());assert(nxb_fg_status(0)<0);assert(!nx_fg_runtime_state());nx_fg_settings_changed();assert(!nxb_fg_available());int handled=1;assert(!nx_fg_present(0,0,&handled));assert(handled==0);assert(!nx_fg_present(0,0,0));assert(!nxb_fg_probe_create(1,1280,720));assert(!nxb_fg_probe_callback());assert(nxb_fg_probe_status(0)<0);return 0;}
''')
 exe=a.output/'fg_disabled_test.exe';subprocess.run([str(a.compiler),'-std=c11','-O2','-I'+str(root/'loader/source'),'-I'+str(a.includes),str(root/'loader/source/nx_fg_disabled.c'),str(source),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True);print('PASS: production disabled FG/probe ABI refuses session/callback allocation, reports unavailable and preserves normal EGL presentation fallback')
if __name__=='__main__':main()
