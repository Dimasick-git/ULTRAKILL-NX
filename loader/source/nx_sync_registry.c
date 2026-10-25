#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "nx_sync_registry.h"
#define NX_SYNC_BUCKETS 4096u
typedef struct Entry { uintptr_t key; void *object; struct Entry *next; } Entry;
static Entry *buckets[NX_SYNC_BUCKETS];
/* Entries remain until process exit, including after object destruction. This
 * makes readers independent of reclamation locks and avoids ABA. Only a small
 * address record is retained; native mutex/condition objects are destroyed. */
static unsigned bucket(uintptr_t key) {
  key^=key>>33;key*=UINT64_C(0xff51afd7ed558ccd);key^=key>>33;
  return (unsigned)(key & (NX_SYNC_BUCKETS-1));
}
static Entry *find(Entry *entry,uintptr_t key) {
  for(;entry;entry=entry->next)if(entry->key==key)return entry;
  return NULL;
}
void *nx_sync_lookup(const void *storage) {
  uintptr_t key=(uintptr_t)storage;if(key<4)return NULL;
  Entry *entry=find(__atomic_load_n(&buckets[bucket(key)],__ATOMIC_ACQUIRE),key);
  return entry?__atomic_load_n(&entry->object,__ATOMIC_ACQUIRE):NULL;
}
void *nx_sync_publish(const void *storage,void *candidate) {
  uintptr_t key=(uintptr_t)storage;if(key<4 || !candidate)return NULL;
  unsigned at=bucket(key);Entry *spare=NULL;
  for(;;) {
    Entry *head=__atomic_load_n(&buckets[at],__ATOMIC_ACQUIRE);
    Entry *entry=find(head,key);
    if(entry) {
      free(spare);
      void *expected=NULL;
      if(__atomic_compare_exchange_n(&entry->object,&expected,candidate,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return candidate;
      return expected;
    }
    if(!spare) {
      spare=calloc(1,sizeof(*spare));if(!spare)return NULL;
      spare->key=key;spare->object=candidate;
    }
    spare->next=head;
    /* Re-scan after a lost CAS: a contender may have installed this SAME key.
     * There is no visible "initializing" owner for another thread to wait on. */
    if(__atomic_compare_exchange_n(&buckets[at],&head,spare,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return candidate;
  }
}
void *nx_sync_remove(const void *storage) {
  uintptr_t key=(uintptr_t)storage;if(key<4)return NULL;
  Entry *entry=find(__atomic_load_n(&buckets[bucket(key)],__ATOMIC_ACQUIRE),key);
  /* POSIX destruction requires that no operation still uses this object. */
  return entry?__atomic_exchange_n(&entry->object,NULL,__ATOMIC_ACQ_REL):NULL;
}
