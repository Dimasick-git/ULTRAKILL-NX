#pragma once
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace nxfg {
inline void* OwnedAligned(size_t alignment,size_t size){
#ifdef _WIN32
 return _aligned_malloc(size,alignment);
#else
 return std::aligned_alloc(alignment,size);
#endif
}
inline void OwnedAlignedFree(void* p){
#ifdef _WIN32
 _aligned_free(p);
#else
 std::free(p);
#endif
}
// Backing is allocated BEFORE entering deko. The release deko library aborts
// on failed internal backing allocations; supplying storage avoids that path.
// Release explicitly after GPU teardown. A quarantined backend keeps storage.
class OwnedMemory {
 struct Node {void* data;Node* next;};Node* head_=nullptr;
 void* (*aligned_)(size_t,size_t);void* (*plain_)(size_t);void (*free_)(void*);
 void (*freeData_)(void*);
public:
 OwnedMemory(void*(*a)(size_t,size_t)=OwnedAligned,void*(*p)(size_t)=std::malloc,void(*f)(void*)=std::free,void(*fd)(void*)=OwnedAlignedFree):aligned_(a),plain_(p),free_(f),freeData_(fd){}
 OwnedMemory(const OwnedMemory&)=delete;
 OwnedMemory& operator=(const OwnedMemory&)=delete;
 void* Allocate(size_t size,size_t alignment){
  if(!size||!alignment||(alignment&(alignment-1))||size>SIZE_MAX-(alignment-1))return nullptr;
  Node* n=(Node*)plain_(sizeof(Node));if(!n)return nullptr;
  n->data=aligned_(alignment,(size+alignment-1)&~(alignment-1));
  if(!n->data){free_(n);return nullptr;}
  n->next=head_;head_=n;return n->data;
 }
 void Release(){while(head_){Node* n=head_;head_=n->next;freeData_(n->data);free_(n);}}
};
}
