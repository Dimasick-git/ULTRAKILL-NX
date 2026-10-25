"""Compile production UI HID, config persistence and installed-path functions."""
from pathlib import Path
import argparse,re,subprocess,tempfile

def function(code,name):
    return re.search(r'^[^\n;]*\b'+name+r'\([^\n]*\) \{.*?^}',code,re.M|re.S).group(0)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc')
    ap.add_argument('--cc-arg',action='append',default=[],help='compiler driver argument, e.g. cc for Zig')
    args=ap.parse_args()
    source=Path(__file__).resolve().parents[1]/'loader/source'
    gameroot=(source/'nx_gameroot.c').read_text(encoding='utf-8')
    bridge=(source/'nx_bridge.c').read_text(encoding='utf-8')
    config=(source/'config.c').read_text(encoding='utf-8')
    config=re.sub(r'^#include "[^"\n]+"\n','',config,flags=re.M)
    cfgtype=re.search(r'typedef struct \{.*?\} Config;', (source/'config.h').read_text(encoding='utf-8'),re.S).group(0)
    code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#ifdef NDEBUG
#error Host tests require active assertions
#endif
static int g_installed=1;
static char g_root[]="sdmc:/switch/ultrakill",g_root_unix[]="/switch/ultrakill";
static void ensure(void){}
static int game_installed(void){return g_installed;}
''' + function(gameroot,'game_content_relative') + '\n' + function(gameroot,'game_path') + '\n' + function(gameroot,'game_content_redirect') + r'''
typedef struct {unsigned finger_id,x,y;} Contact;
typedef struct {unsigned count;Contact touches[16];} HidTouchScreenState;
typedef struct {uint32_t down,id;float x,y;} NxbTouch;
static HidTouchScreenState incoming;
static int hidGetTouchScreenStates(HidTouchScreenState *out,int n){(void)n;*out=incoming;return 1;}
static int nxb_touch_ui_active;
''' + function(bridge,'nxb_touch') + '\n' + cfgtype + '\n' + config + r'''
static int render_changes,boost_ended;
static int observed_height=540,observed_filter=1;static float observed_sharp=.5f;
static int nx_render_height(void){return observed_height;}
static int nx_render_filter(void){return observed_filter;}
static float nx_render_sharpness(void){return observed_sharp;}
static void nx_render_config_changed(void){render_changes++;observed_height=config.render_height;observed_filter=config.fsr;observed_sharp=config.fsr_sharpness;}
static void nxb_loading(int active){if(!active)boost_ended++;}
static void nx_loading_settings_changed(void){if(!config.loading_boost)nxb_loading(0);}
static int nxb_fg_available(void){return 0;}
static void nx_fg_settings_changed(void){}
static const char *config_dest="config.txt";
static void config_path(char *buf,size_t sz,const char *rel){(void)rel;snprintf(buf,sz,"%s",config_dest);}
#define CONFIG_NAME "config.txt"
#define game_path config_path
''' + function(bridge,'nxb_config_set') + r'''
int main(void){
 FILE *language_file=fopen("language-test.txt","w");assert(language_file);fclose(language_file);
 assert(read_config("language-test.txt")>=0 && config.ui_language==1);
 language_file=fopen("language-test.txt","w");assert(language_file);
 fputs("ui_language 0\n",language_file);fclose(language_file);
 assert(read_config("language-test.txt")>=0 && config.ui_language==1 && config.ui_language_revision==1);
 language_file=fopen("language-test.txt","w");assert(language_file);
 fputs("ui_language 0\nui_language_revision 1\n",language_file);fclose(language_file);
 assert(read_config("language-test.txt")>=0 && config.ui_language==0);
 language_file=fopen("language-test.txt","w");assert(language_file);
 fputs("ui_language 2\n",language_file);fclose(language_file);
 assert(read_config("language-test.txt")>=0 && config.ui_language==2);
 language_file=fopen("language-test.txt","w");assert(language_file);
 fputs("fsr 1\nfsr3 1\nframe_generation 1\nfg_probe 1\n",language_file);fclose(language_file);
 assert(read_config("language-test.txt")>=0 && config.fsr==1 && !config.fsr3 && !config.frame_generation && !config.fg_probe);
 char result[512];
 const char *assets[]={"assets/bin/Data/globalgamemanagers","assets/aa/catalog.json","assets/VideosNX/intro.nxv","libil2cpp.so","offsets.ini"};
 for(unsigned i=0;i<sizeof assets/sizeof *assets;i++){
   assert(game_content_relative(assets[i]));
   assert(game_content_redirect(assets[i],result,sizeof result)==result);
   assert(!strncmp(result,"nxdata:/ultrakill/",17));
 }
 const char *mutable[]={"Saves/Slot1/save.json","Preferences/Prefs.json","config.txt","il2cpp/Metadata/global-metadata.dat","shader-errors/log.txt","assets/../Saves/file"};
 for(unsigned i=0;i<sizeof mutable/sizeof *mutable;i++)assert(!game_content_relative(mutable[i]));
 assert(game_content_redirect("/switch/ultrakill/assets/loc/en.json",result,sizeof result)==result);
 assert(!strcmp(result,"nxdata:/ultrakill/assets/loc/en.json"));
 assert(game_content_redirect("sdmc:/switch/ultrakill/assets/aa/x",result,sizeof result)==result);
 const char *neighbor="sdmc:/switch/ultrakill-old/assets/aa/x";
 assert(game_content_redirect(neighbor,result,sizeof result)==neighbor);
 g_installed=0;assert(game_content_redirect(assets[0],result,sizeof result)==assets[0]);g_installed=1;

 NxbTouch touch;assert(nxb_touch(&touch) && !touch.down && nxb_touch_ui_active);
 incoming.count=1;incoming.touches[0]=(Contact){0,1280,720};
 assert(nxb_touch(&touch) && touch.down && touch.id==0 && touch.x==1280 && touch.y==720);
 incoming.count=2;incoming.touches[1]=incoming.touches[0];incoming.touches[0]=(Contact){7,50,90};
 assert(nxb_touch(&touch) && touch.id==0);
 incoming.count=1;assert(nxb_touch(&touch) && !touch.down && touch.id==0 && touch.x==1280 && touch.y==720);
 assert(nxb_touch(&touch) && touch.down && touch.id==7 && touch.x==50 && touch.y==90);
 incoming.touches[0]=(Contact){7,620,430};
 assert(nxb_touch(&touch) && touch.down && touch.x==620 && touch.y==430);
 incoming.count=0;assert(nxb_touch(&touch) && !touch.down && touch.id==7 && touch.x==620 && touch.y==430);
 assert(nxb_touch(&touch) && !touch.down && touch.x==620 && touch.y==430);assert(!nxb_touch(NULL));

 config.gyro=1;config.render_height=540;config.fsr=1;config.gyro_sens=2;
 Config before=config;
 assert(!nxb_config_set("fsr3",1));assert(!nxb_config_set("frame_generation",1));
 assert(!nxb_config_set("physics_hz",60));assert(!nxb_config_set("gyro",2));
 assert(!nxb_config_set("render_height",179));assert(!nxb_config_set("render_height",540.5f));
 assert(!nxb_config_set("gyro_sens",NAN));assert(!nxb_config_set("fsr_sharpness",INFINITY));
 assert(!memcmp(&before,&config,sizeof config));
 assert(nxb_config_set("render_height",480) && config.render_height==480 && render_changes==1);
 assert(nxb_config_set("gyro_sens",3.25f));assert(nxb_config_set("loading_boost",0) && boost_ended);
 assert(read_config("config.txt")>=0 && config.render_height==480 && config.gyro_sens==3.25f);
 observed_height=360;observed_filter=0;observed_sharp=.8f;
 assert(nxb_config_set("fsr_sharpness",1) && config.render_height==360 && config.fsr==0 && config.fsr_sharpness==1);
 before=config;config_dest="missing/config.txt";
 assert(!nxb_config_set("gyro",0));assert(!memcmp(&before,&config,sizeof config));
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';src.write_text(code,encoding='utf-8')
        subprocess.run([args.cc,*args.cc_arg,'-std=c11','-O2','-UNDEBUG','-Wall',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=tmp,check=True)
    print('PASS: English default/migration, explicit language retention, touch release, installed paths, validated settings, persistence and failure rollback')

if __name__=='__main__':main()
