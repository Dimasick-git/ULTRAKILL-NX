#pragma once
#include <cstdint>
#include <algorithm>
namespace nx {
// Rendering and generated frames never advance gameplay. A work limit retains
// backlog instead of dropping simulation time or speeding up the next frame.
class FixedClock {
 static constexpr uint64_t Step=8000000;
 uint64_t accumulated=0;double fractional=0;bool paused=false;
public:
 uint64_t ticks=0;
 void setPaused(bool value){paused=value;}
 void advance(uint64_t realNanoseconds,double timeScale=1){
  if(paused||!(timeScale>0))return;
  double scaled=double(realNanoseconds)*timeScale+fractional;
  const double room=double(UINT64_MAX-accumulated);
  if(scaled>=room){accumulated=UINT64_MAX;fractional=0;return;}
  uint64_t whole=uint64_t(scaled);fractional=scaled-whole;accumulated+=whole;
 }
 template<class Function> unsigned run(unsigned budget,Function&&tick){
  if(paused)return 0;
  unsigned count=0;
  while(count<budget && accumulated>=Step){tick(.008f);accumulated-=Step;ticks++;count++;}
  return count;
 }
 double interpolation()const{return std::min(1.0,double(accumulated)/Step);}
 uint64_t backlogNanoseconds()const{return accumulated;}
};
}
