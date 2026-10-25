#include "nx_clock.h"
#include <cassert>
#include <cstdio>
unsigned run(unsigned hz){nx::FixedClock clock;for(unsigned i=1;i<=hz*10;i++){uint64_t now=uint64_t(i)*1000000000/hz,before=uint64_t(i-1)*1000000000/hz;clock.advance(now-before);clock.run(100,[](float dt){assert(dt==.008f);});}assert(clock.backlogNanoseconds()==0);return clock.ticks;}
int main(){
 for(auto hz:{20,30,60,120})assert(run(hz)==1250);
 nx::FixedClock clock;clock.advance(1000000000);assert(clock.run(4,[](float){})==4);assert(clock.backlogNanoseconds()==968000000);
 assert(clock.run(1000,[](float){})==121 && clock.ticks==125);
 clock.advance(8000000);clock.setPaused(true);clock.advance(1000000000);assert(clock.run(1000,[](float){})==0 && clock.backlogNanoseconds()==8000000);clock.setPaused(false);assert(clock.run(1000,[](float){})==1);
 clock.advance(1000000000,.5);assert(clock.run(1000,[](float){})==62);clock.advance(1000000000,.5);assert(clock.run(1000,[](float){})==63);
 assert(clock.ticks==251 && clock.backlogNanoseconds()==0);
 std::puts("PASS: 1250 ticks in ten seconds at 20/30/60/120 render FPS; budget retains backlog; pause/time scale without lost or doubled time");
}
