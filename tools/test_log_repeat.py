"""Exercise the production duplicate-error gate, including changed errors."""
import argparse, subprocess, tempfile
from pathlib import Path

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cc', default='gcc')
    args = ap.parse_args()
    header = Path(__file__).resolve().parents[1] / 'loader/source/nx_log_repeat.h'
    source = '#include <assert.h>\n' + header.read_text() + r'''
int main(void) {
 NxLogRepeat s={0}; unsigned n;
 for(unsigned i=0;i<4;++i) assert(nx_log_repeat(&s,"NullReferenceException",i,100,&n)&&n==0);
 for(unsigned i=4;i<20;++i) assert(!nx_log_repeat(&s,"NullReferenceException",i,100,&n));
 assert(nx_log_repeat(&s,"NullReferenceException",100,100,&n)&&n==16);
 assert(!nx_log_repeat(&s,"NullReferenceException",101,100,&n));
 assert(nx_log_repeat(&s,"OtherException",102,100,&n)&&n==1);
 assert(nx_log_repeat(&s,"OtherException",103,100,&n)&&n==0);
 char long_error[4200]; memset(long_error,'x',sizeof(long_error)-1);long_error[4199]=0;
 assert(nx_log_repeat(&s,long_error,104,100,&n));
 assert(nx_log_repeat(&s,long_error,105,100,&n));
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp)/'test.c'; exe = Path(tmp)/'test.exe'; c.write_text(source)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('PASS: initial samples, repeat counts, interval, changed and long errors')

if __name__ == '__main__': main()
