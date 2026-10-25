"""Fault-inject the actual patched Nouveau slab allocator on the host."""
import argparse,re,subprocess,tempfile
from pathlib import Path

STUB = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "util/list.h"
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define ffs __builtin_ffs
#define PIPE_OK 0
#define PIPE_ERROR_OUT_OF_MEMORY -1
#define FREE free
static int fail_malloc, fail_bo, live_bo;
static void *test_malloc(size_t size) { if(fail_malloc && --fail_malloc==0) return NULL; return malloc(size); }
#define MALLOC test_malloc
#define MALLOC_STRUCT(t) test_malloc(sizeof(struct t))
#define debug_printf(...) ((void)0)
static int nouveau_mesa_debug;
struct nouveau_device {int unused;};
union nouveau_bo_config {struct {unsigned memtype;} nv50;};
struct nouveau_bo {uint64_t size;int refs;};
struct nouveau_mm_allocation {struct nouveau_mm_allocation *next;void *priv;uint32_t offset;};
struct nouveau_mman;
static uint64_t nouveau_mm_trim(struct nouveau_mman *);
static int nouveau_bo_new(struct nouveau_device *dev,uint32_t domain,unsigned align,uint32_t size,union nouveau_bo_config *conf,struct nouveau_bo **out) {
 if(fail_bo)return -12;
 *out=calloc(1,sizeof(**out));assert(*out);(*out)->size=size;(*out)->refs=1;live_bo++;return 0;
}
static void nouveau_bo_ref(struct nouveau_bo *bo,struct nouveau_bo **out) {
 if(bo)bo->refs++;
 if(*out && --(*out)->refs==0){free(*out);live_bo--;}
 *out=bo;
}
'''

TEST = r'''
int main(void) {
 struct nouveau_device device={0};union nouveau_bo_config config={0};
 struct nouveau_mman *cache=nouveau_mm_create(&device,1,&config);
 struct nouveau_bo *bo=NULL;uint32_t offset=0;
 fail_bo=1;
 assert(nouveau_mm_allocate(cache,1024,&bo,&offset)==NULL && bo==NULL);
 assert(cache->allocated==0 && live_bo==0); // failed slab creation must not use list head as a slab
 fail_bo=0;
 struct nouveau_mm_allocation *a=nouveau_mm_allocate(cache,1024,&bo,&offset);assert(a && bo);
 assert(nouveau_mm_trim(cache)==0 && live_bo==1); // live allocation must survive trim
 nouveau_mm_free(a);nouveau_bo_ref(NULL,&bo);
 uint64_t bytes=nouveau_mm_trim(cache);assert(bytes>0 && cache->allocated==0 && live_bo==0);
 // Slab metadata, BO allocation, then allocation-token failure: roll back the chunk and list.
 fail_malloc=2;
 assert(nouveau_mm_allocate(cache,1024,&bo,&offset)==NULL && bo==NULL);
 fail_malloc=0;
 assert(nouveau_mm_trim(cache)>0 && cache->allocated==0 && live_bo==0);
 for(int i=0;i<100;i++) {
   a=nouveau_mm_allocate(cache,128u<<(i%10),&bo,&offset);assert(a && bo);
   nouveau_mm_free(a);nouveau_bo_ref(NULL,&bo);
   nouveau_mm_trim(cache);assert(live_bo==0 && cache->allocated==0);
 }
 // OOM in a different bucket releases wholly unused cached slabs, then fails cleanly.
 a=nouveau_mm_allocate(cache,1024,&bo,&offset);assert(a);nouveau_mm_free(a);nouveau_bo_ref(NULL,&bo);
 fail_bo=1;assert(!nouveau_mm_allocate(cache,65536,&bo,&offset) && !bo);assert(live_bo==0);
 fail_bo=0;nouveau_mm_destroy(cache);
 puts("PASS: slab OOM, token OOM rollback, live-buffer retention, cache eviction and 100 allocation/trim cycles");
 return 0;
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--mesa-source',required=True,type=Path)
    ap.add_argument('--cc',required=True);ap.add_argument('--cc-arg',action='append',default=[]);a=ap.parse_args()
    source=(a.mesa_source/'src/gallium/drivers/nouveau/nouveau_mm.c').read_text()
    source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
    # The forward declaration is static only for this standalone harness.
    source=source.replace('uint64_t\nnouveau_mm_trim(', 'static uint64_t\nnouveau_mm_trim(')
    with tempfile.TemporaryDirectory(prefix='nx-mesa-memory-') as temp:
        path=Path(temp);(path/'test.c').write_text(STUB+'\n'+source+'\n'+TEST)
        binary=path/'test.exe'
        subprocess.run([a.cc,*a.cc_arg,'-std=gnu11','-O1','-g','-I'+str(a.mesa_source/'src'),'-I'+str(a.mesa_source/'include'),str(path/'test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
if __name__=='__main__':main()
