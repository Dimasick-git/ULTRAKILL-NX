#pragma once
#include <stdint.h>
// Render-thread only. Marker and consumer are ordered Unity plugin events,
// after world copies and after composition respectively; no CPU frame guess.
struct NxFgCapture {
 uint64_t produced=0,consumed=0;
 void Mark(){++produced;}
 bool Consume(){
  if(!produced||produced==consumed)return false;
  consumed=produced;return true;
 }
};
