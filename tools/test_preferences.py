from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
    code=(ROOT/'loader/source/nx_preferences.c').read_text()
    code=re.sub(r'^#include "[^\n]+"\n','',code,flags=re.M)
    pre=r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
static struct {int performance_mode;} config;
static int game_path(char *s,size_t n,const char *rel){return snprintf(s,n,"%s",rel);}
#define debugPrintf(...) ((void)0)
#ifdef _WIN32
#define mkdir(p,m) mkdir(p)
#endif
'''
    test=r'''
int main(void){
 config.performance_mode=0;nx_preferences_prepare();struct stat st;assert(stat("Preferences/LocalPrefs.json",&st)!=0);
 config.performance_mode=1;nx_preferences_prepare();assert(!stat("Preferences/LocalPrefs.json",&st)&&st.st_size>0);
 char s[256]={0};FILE*f=fopen("Preferences/LocalPrefs.json","rb");fread(s,1,sizeof s-1,f);fclose(f);
 assert(strstr(s,"\"maxGore\":64.0")&&strstr(s,"\"vSync\":false"));
 f=fopen("Preferences/LocalPrefs.json","wb");fputs("{\"maxGore\":200,\"custom\":true}",f);fclose(f);
 nx_preferences_prepare();memset(s,0,sizeof s);f=fopen("Preferences/LocalPrefs.json","rb");fread(s,1,sizeof s-1,f);fclose(f);
 assert(!strcmp(s,"{\"maxGore\":200,\"custom\":true}"));
 f=fopen("Preferences/LocalPrefs.json","wb");fclose(f);nx_preferences_prepare();assert(!stat("Preferences/LocalPrefs.json",&st)&&st.st_size>0);
 assert(stat("Preferences/LocalPrefs.json.nx-defaults",&st)!=0);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';p.write_text(pre+code+test)
        subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=tmp,check=True)
    print('PASS: clean/empty local preferences get old SD defaults, existing custom preferences and disabled profile preserved')
if __name__=='__main__':main()
