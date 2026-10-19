/* Optional SaltyNX support in the full NSP. Fill missing files only; existing
 * system modules, user exceptions and clock configuration are never replaced. */
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "nx_gameroot.h"
#include "util.h"

int nx_support_reboot_required;

static int allowed(const char *s) {
  if (!s || strstr(s,"..") || strchr(s,'\\') || strchr(s,':')) return 0;
  return !strncmp(s,"atmosphere/contents/0000000000534C56/",37) ||
    !strncmp(s,"atmosphere/exefs_patches/SaltyNX_Fixes/",39) ||
    !strncmp(s,"SaltySD/",8);
}
static void parents(char *path) {
  for (char *s=path+6; *s; ++s) if(*s=='/') { *s=0;mkdir(path,0777);*s='/'; }
}

void nx_support_prepare(void) {
  if (!game_installed()) return;
  FILE *manifest=fopen("nxdata:/support.list","r");if(!manifest)return;
  struct stat st;
  const char *marker="sdmc:/switch/ultrakill/.support-installing";
  int existing=stat("sdmc:/atmosphere/contents/0000000000534C56/exefs.nsp",&st)==0 && stat(marker,&st)!=0;
  if(!existing) { FILE *m=fopen(marker,"wb");if(!m){fclose(manifest);return;}fclose(m); }
  char line[512],rel[256],hex[65],src[320],dst[320],tmp[336];
  unsigned long long size;
  int failed=0,added=0;
  unsigned char buffer[16384],hash[32],expected[32];
  while(fgets(line,sizeof line,manifest)) {
    if(sscanf(line,"%llu %64s %255s",&size,hex,rel)!=3 || strlen(hex)!=64 || !allowed(rel)) { failed=1;break; }
    if(existing && !strncmp(rel,"atmosphere/contents/",20))continue;
    snprintf(dst,sizeof dst,"sdmc:/%s",rel);
    if(stat(dst,&st)==0)continue;
    snprintf(src,sizeof src,"nxdata:/support/%s",rel);
    snprintf(tmp,sizeof tmp,"%s.nx-tmp",dst);parents(tmp);
    FILE *in=fopen(src,"rb"),*out=fopen(tmp,"wb");
    if(!in || !out) { if(in)fclose(in);if(out)fclose(out);failed=1;break; }
    Sha256Context sha;sha256ContextCreate(&sha);
    unsigned long long copied=0;size_t n;int error=0;
    while((n=fread(buffer,1,sizeof buffer,in))) {
      sha256ContextUpdate(&sha,buffer,n);copied+=n;
      if(fwrite(buffer,1,n,out)!=n){error=1;break;}
    }
    if(ferror(in) || ferror(out))error=1;
    fclose(in);if(fclose(out)!=0)error=1;
    sha256ContextGetHash(&sha,hash);
    for(int i=0;i<32;i++){unsigned b;if(sscanf(hex+i*2,"%2x",&b)!=1){error=1;b=0;}expected[i]=b;}
    if(error || copied!=size || memcmp(hash,expected,32) || rename(tmp,dst)!=0) {
      remove(tmp);failed=1;break;
    }
    added++;
  }
  fclose(manifest);
  /* The manifest puts boot2.flag last. A partial install is never enabled. */
  if(!existing && !failed) { nx_support_reboot_required=1;remove(marker); }
  debugPrintf("[support] added=%d failed=%d existing_module=%d reboot_required=%d\n",added,failed,existing,nx_support_reboot_required);
}
