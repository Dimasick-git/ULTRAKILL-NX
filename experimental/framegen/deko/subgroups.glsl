// Maxwell/UAM adapter for the subgroup operations used by AMD FSR 3.1.
// Hardware subgroup size is 32. Masks preserve active-invocation semantics.
#define gl_SubgroupInvocationID gl_SubGroupInvocationARB
#define gl_SubgroupSize 32u
uint nxActiveMask() { return uint(ballotARB(true)); }
bool subgroupElect() { return gl_SubGroupInvocationARB == uint(findLSB(nxActiveMask())); }
uvec4 subgroupBallot(bool v) { return uvec4(uint(ballotARB(v)),0u,0u,0u); }
uint subgroupBallotBitCount(uvec4 m) { return uint(bitCount(m.x)); }
uint subgroupBallotExclusiveBitCount(uvec4 m) { return uint(bitCount(m.x & ((1u << gl_SubGroupInvocationARB)-1u))); }
bool subgroupAll(bool v) { return uint(ballotARB(v)) == nxActiveMask(); }
bool subgroupShuffle(bool v,uint i) { return readInvocationARB(v?1u:0u,i)!=0u; }
bool subgroupBroadcastFirst(bool v) { return readFirstInvocationARB(v?1u:0u)!=0u; }
float subgroupShuffleXor(float v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
float subgroupBroadcastFirst(float v) { return readFirstInvocationARB(v); }
float subgroupQuadSwapHorizontal(float v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
float subgroupQuadSwapVertical(float v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
float subgroupQuadSwapDiagonal(float v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
vec2 subgroupShuffleXor(vec2 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
vec2 subgroupBroadcastFirst(vec2 v) { return readFirstInvocationARB(v); }
vec2 subgroupQuadSwapHorizontal(vec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
vec2 subgroupQuadSwapVertical(vec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
vec2 subgroupQuadSwapDiagonal(vec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
vec3 subgroupShuffleXor(vec3 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
vec3 subgroupBroadcastFirst(vec3 v) { return readFirstInvocationARB(v); }
vec3 subgroupQuadSwapHorizontal(vec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
vec3 subgroupQuadSwapVertical(vec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
vec3 subgroupQuadSwapDiagonal(vec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
vec4 subgroupShuffleXor(vec4 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
vec4 subgroupBroadcastFirst(vec4 v) { return readFirstInvocationARB(v); }
vec4 subgroupQuadSwapHorizontal(vec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
vec4 subgroupQuadSwapVertical(vec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
vec4 subgroupQuadSwapDiagonal(vec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
uint subgroupShuffleXor(uint v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
uint subgroupBroadcastFirst(uint v) { return readFirstInvocationARB(v); }
uint subgroupQuadSwapHorizontal(uint v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
uint subgroupQuadSwapVertical(uint v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
uint subgroupQuadSwapDiagonal(uint v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
uvec2 subgroupShuffleXor(uvec2 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
uvec2 subgroupBroadcastFirst(uvec2 v) { return readFirstInvocationARB(v); }
uvec2 subgroupQuadSwapHorizontal(uvec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
uvec2 subgroupQuadSwapVertical(uvec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
uvec2 subgroupQuadSwapDiagonal(uvec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
uvec3 subgroupShuffleXor(uvec3 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
uvec3 subgroupBroadcastFirst(uvec3 v) { return readFirstInvocationARB(v); }
uvec3 subgroupQuadSwapHorizontal(uvec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
uvec3 subgroupQuadSwapVertical(uvec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
uvec3 subgroupQuadSwapDiagonal(uvec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
uvec4 subgroupShuffleXor(uvec4 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
uvec4 subgroupBroadcastFirst(uvec4 v) { return readFirstInvocationARB(v); }
uvec4 subgroupQuadSwapHorizontal(uvec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
uvec4 subgroupQuadSwapVertical(uvec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
uvec4 subgroupQuadSwapDiagonal(uvec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
int subgroupShuffleXor(int v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
int subgroupBroadcastFirst(int v) { return readFirstInvocationARB(v); }
int subgroupQuadSwapHorizontal(int v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
int subgroupQuadSwapVertical(int v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
int subgroupQuadSwapDiagonal(int v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
ivec2 subgroupShuffleXor(ivec2 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
ivec2 subgroupBroadcastFirst(ivec2 v) { return readFirstInvocationARB(v); }
ivec2 subgroupQuadSwapHorizontal(ivec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
ivec2 subgroupQuadSwapVertical(ivec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
ivec2 subgroupQuadSwapDiagonal(ivec2 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
ivec3 subgroupShuffleXor(ivec3 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
ivec3 subgroupBroadcastFirst(ivec3 v) { return readFirstInvocationARB(v); }
ivec3 subgroupQuadSwapHorizontal(ivec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
ivec3 subgroupQuadSwapVertical(ivec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
ivec3 subgroupQuadSwapDiagonal(ivec3 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
ivec4 subgroupShuffleXor(ivec4 v,uint x) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ x); }
ivec4 subgroupBroadcastFirst(ivec4 v) { return readFirstInvocationARB(v); }
ivec4 subgroupQuadSwapHorizontal(ivec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u); }
ivec4 subgroupQuadSwapVertical(ivec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u); }
ivec4 subgroupQuadSwapDiagonal(ivec4 v) { return readInvocationARB(v,gl_SubGroupInvocationARB ^ 3u); }
float subgroupAdd(float v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
float result=float(0);
for(uint i=0u;i<32u;++i) { float other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=(result + other); }
return result; }
float subgroupMin(float v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
float result=float(3.402823466e+38);
for(uint i=0u;i<32u;++i) { float other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=min(result,other); }
return result; }
float subgroupMax(float v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
float result=float(-3.402823466e+38);
for(uint i=0u;i<32u;++i) { float other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=max(result,other); }
return result; }
uint subgroupAdd(uint v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=(v + readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
uint result=uint(0);
for(uint i=0u;i<32u;++i) { uint other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=(result + other); }
return result; }
uint subgroupMin(uint v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=min(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
uint result=uint(4294967295u);
for(uint i=0u;i<32u;++i) { uint other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=min(result,other); }
return result; }
uint subgroupMax(uint v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=max(v,readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
uint result=uint(0u);
for(uint i=0u;i<32u;++i) { uint other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=max(result,other); }
return result; }
uint subgroupOr(uint v) {
uint mask=nxActiveMask();
if(mask==0xffffffffu) {
v=(v | readInvocationARB(v,gl_SubGroupInvocationARB ^ 1u));
v=(v | readInvocationARB(v,gl_SubGroupInvocationARB ^ 2u));
v=(v | readInvocationARB(v,gl_SubGroupInvocationARB ^ 4u));
v=(v | readInvocationARB(v,gl_SubGroupInvocationARB ^ 8u));
v=(v | readInvocationARB(v,gl_SubGroupInvocationARB ^ 16u));
return v; }
uint result=uint(0u);
for(uint i=0u;i<32u;++i) { uint other=readInvocationARB(v,i); if((mask & (1u<<i))!=0u) result=(result | other); }
return result; }
