// Internal link/runtime check. This is not packaged as a game installer.
#include "nx_physics.h"
#include <cstdio>
int main(){
 using namespace nx::game;
 FILE*log=std::fopen("sdmc:/ultrakill-native-physics.log","w");
 if(!log)return 1;
 std::fprintf(log,"Native physics adapter check; not the game\n");std::fflush(log);
 PhysicsPack pack;auto&p=pack.player;p.objectId=1;p.position={0,5,0};p.up={0,1,0};p.forward={0,0,1};p.walkSpeed=750;p.airAcceleration=6000;p.jumpPower=90;p.wallJumpPower=150;p.mass=100;p.radius=.5f;p.height=3.5f;p.center={0,.25f,0};p.gravity={0,-40,0};p.frictionCombine=p.bounceCombine=1;pack.layerMasks.fill(~0u);
 ColliderRecord floor;floor.id=2;floor.objectId=2;floor.kind=Box;floor.flags=Active|Enabled;floor.layer=8;floor.world[0]=floor.world[5]=floor.world[10]=1;floor.center[1]=-.5f;floor.size[0]=floor.size[2]=100;floor.size[1]=1;pack.colliders.push_back(floor);
 std::string error;bool okay=true;
 for(unsigned workers:{0u,2u}){
  PhysicsWorld world;okay=world.initialize(pack,workers,error);
  for(unsigned tick=0;okay&&tick<250;tick++)okay=world.step(.008f,error);
  auto pos=world.playerPosition();okay=okay&&std::abs(pos.y-1.5f)<.03f;
  std::fprintf(log,"workers=%u result=%s position=(%.4f %.4f %.4f) error=%s\n",workers,okay?"PASS":"FAIL",pos.x,pos.y,pos.z,error.c_str());std::fflush(log);
  if(!okay)break;
 }
 std::fprintf(log,"%s\n",okay?"ALL CPU PHYSICS CHECKS PASSED":"FAIL");std::fclose(log);return okay?0:1;
}
