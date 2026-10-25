#include "nx_session.h"
#include "nx_clock.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace nx::game;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(false)
int main(int argc,char**argv){
 CHECK(argc>=2&&argc<=5);PhysicsPack pack;const char*error=nullptr;CHECK(pack.load(argv[1],&error));std::string failure;GraphPack graph;if(argc>=3)CHECK(graph.load(argv[2],failure));ActorPack actors;if(argc>=4)CHECK(actors.load(argv[3],failure));JointPack joints;if(argc==5)CHECK(joints.load(argv[4],failure));NativeSession session(pack,argc>=3?&graph:nullptr,argc>=4?&actors:nullptr,argc==5?&joints:nullptr);
 CHECK(session.initialize(2,failure));for(unsigned i=0;i<500;i++){if(!session.tick(0,0,0,.008f,failure)){std::fprintf(stderr,"session: %s\n",failure.c_str());CHECK(false);}}
 CHECK(session.grounded());auto start=session.position();CHECK(finite(start));
 for(unsigned i=0;i<50;i++)CHECK(session.tick(0,1,0,.008f,failure));auto finish=session.position();CHECK(finite(finish));CHECK(finish.z>start.z+.5f);
 CHECK(!session.tick(NAN,0,0,.008f,failure));CHECK(!session.tick(0,0,0,.016f,failure));
 CHECK(session.initialize(0,failure));for(unsigned i=0;i<500;i++)CHECK(session.tick(0,0,0,.008f,failure));
 auto jumpStart=session.position();CHECK(session.grounded());CHECK(!session.requestJump(NAN));CHECK(!session.requestJump(-1));
 CHECK(session.requestJump(10));CHECK(session.requestJump(11)); // first edge retained
 CHECK(!session.jumpState().jumping);CHECK(session.tick(0,0,0,.008f,failure));
 CHECK(session.jumpState().jumping&&session.jumpState().jumpCooldown);CHECK(session.jumpState().jumpTimestamp==10);
 CHECK(session.velocity().y>20);CHECK(session.effectRequests().size()==2);session.clearEffectRequests();
 float maximum=session.position().y;
 for(unsigned i=0;i<250;i++){CHECK(session.tick(0,0,0,.008f,failure));maximum=std::max(maximum,session.position().y);CHECK(session.effectRequests().empty());}
 CHECK(maximum>jumpStart.y+5);CHECK(session.grounded());CHECK(!session.jumpState().jumping&&!session.jumpState().jumpCooldown&&!session.jumpState().falling);
 CHECK(session.requestJump(12));session.clearPendingInput();CHECK(session.tick(0,0,0,.008f,failure));CHECK(!session.jumpState().jumping&&session.effectRequests().empty());
 // A press delivered on a 240 Hz render frame with zero simulation steps
 // must survive until the next tick, including a low-FPS catch-up frame.
 for(unsigned fps:{20u,30u,60u,120u,240u}){
  CHECK(session.initialize(0,failure));for(unsigned i=0;i<500;i++)CHECK(session.tick(0,0,0,.008f,failure));
  nx::FixedClock clock;CHECK(session.requestJump(20));unsigned requests=0;
  for(unsigned frame=0;frame<fps;frame++){
   uint64_t ns=(uint64_t(frame+1)*1000000000/fps)-(uint64_t(frame)*1000000000/fps);clock.advance(ns);
   clock.run(8,[&](float dt){CHECK(session.tick(0,0,0,dt,failure));});requests+=unsigned(session.effectRequests().size());session.clearEffectRequests();
  }
  CHECK(clock.ticks==125&&requests==2);CHECK(session.jumpState().jumpTimestamp==20);
 }
 std::puts("PASS: integrated ordinary jump, airborne arc/landing, single-consumption latched input at 20/30/60/120/240 Hz, pause cancellation and deferred effect requests");
 CHECK(session.initialize(0,failure));for(unsigned i=0;i<500;i++)CHECK(session.tick(0,0,0,.008f,failure));
 session.requestDash();CHECK(session.tick(0,0,0,.008f,failure));CHECK(session.jumpState().boost);CHECK(session.collisionLayer()==15);if(auto*g=session.sceneGraph())CHECK(g->node(g->find(pack.player.objectId))->local.layer==15);CHECK(session.velocity().z>40);CHECK(session.dashEffectRequests().size()==3);session.clearEffectRequests();
 for(unsigned i=1;i<25;i++){CHECK(session.tick(0,0,0,.008f,failure));CHECK(session.jumpState().boost&&session.dashEffectRequests().empty());}
 CHECK(std::abs(session.dashState().boostLeft)<.001f);CHECK(session.tick(0,0,0,.008f,failure));CHECK(!session.jumpState().boost);CHECK(session.tick(0,0,0,.008f,failure));CHECK(session.collisionLayer()==2);
 session.requestDash();session.clearPendingInput();CHECK(session.tick(0,0,0,.008f,failure));CHECK(!session.jumpState().boost&&session.dashEffectRequests().empty());
 std::puts("PASS: integrated ordinary dash speed/duration, collision layer 2 -> 15 -> 2, effect requests and pause cancellation; original full Update/FixedUpdate scheduling still under migration");
 CHECK(session.initialize(0,failure));for(unsigned i=0;i<500;i++)CHECK(session.tick(0,0,0,.008f,failure));auto slideStart=session.position();
 session.setSlideHeld(true);CHECK(session.requestSlide(30));CHECK(session.tick(0,1,0,.008f,failure));CHECK(session.jumpState().sliding&&session.jumpState().boost);CHECK(session.capsuleHeight()==1.25f);CHECK(session.position().y<slideStart.y-.5f);session.clearEffectRequests();
 for(unsigned i=0;i<8;i++){CHECK(session.tick(0,1,0,.008f,failure));session.clearEffectRequests();}CHECK(session.velocity().z>20);
 session.setSlideHeld(false);CHECK(session.tick(0,0,0,.008f,failure));CHECK(!session.jumpState().sliding);session.clearEffectRequests();
 for(unsigned i=0;i<100;i++){CHECK(session.tick(0,0,0,.008f,failure));session.clearEffectRequests();}
 CHECK(session.capsuleHeight()==3.5f);CHECK(!session.crouchState().crouching&&!session.crouchState().slowMode);CHECK(session.grounded());
 session.setSlideHeld(true);CHECK(session.requestSlide(31));CHECK(session.tick(0,1,0,.008f,failure));session.clearEffectRequests();CHECK(session.jumpState().sliding);
 CHECK(session.requestJump(31.02));CHECK(session.tick(0,1,0,.008f,failure));CHECK(!session.jumpState().sliding&&session.jumpState().jumping);CHECK(session.velocity().y>10);session.clearEffectRequests();
 CHECK(session.initialize(0,failure));for(unsigned i=0;i<500;i++)CHECK(session.tick(0,0,0,.008f,failure));session.setSlideHeld(true);CHECK(session.requestSlide(32));CHECK(session.tick(0,1,0,.008f,failure));session.clearEffectRequests();CHECK(session.jumpState().sliding);
 session.clearPendingInput();session.clearPendingInput();session.clearPendingInput();CHECK(session.tick(0,0,0,.008f,failure));CHECK(!session.jumpState().sliding);session.clearEffectRequests();
 std::puts("PASS: actual level ordinary slide capsule/probe shrink, speed, release/stand and slide-jump; complete portal/wall/effect lifecycle and console verification still pending");
 if(auto*hierarchy=session.sceneGraph()){
  auto*player=hierarchy->node(hierarchy->find(pack.player.objectId));CHECK(player);auto pos=session.position();CHECK(std::abs(player->world.m[12]-pos.x)<.0001f&&std::abs(player->world.m[13]-pos.y)<.0001f&&std::abs(player->world.m[14]-pos.z)<.0001f);
  CHECK(hierarchy->activationEvents().empty());CHECK(session.initialize(0,failure));player=session.sceneGraph()->node(session.sceneGraph()->find(pack.player.objectId));CHECK(std::abs(player->world.m[13]-pack.player.position.y)<.0001f);
  std::puts("PASS: player/probe authored hierarchy bindings follow native simulation and reset; original scene scripts and render/physics activation are not connected");
 }
 std::printf("PASS: actual level integrated sensor bookkeeping -> original Move kernel -> native solver: start=(%.3f %.3f %.3f) finish=(%.3f %.3f %.3f); not complete gameplay or Switch runtime verification\n",start.x,start.y,start.z,finish.x,finish.y,finish.z);
}
