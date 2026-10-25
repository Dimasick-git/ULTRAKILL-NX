layout(local_size_x=64) in;
layout(std430,binding=0) buffer Results { uvec4 values[]; } results;
void main(){
 uint id=gl_LocalInvocationIndex, lane=gl_SubGroupInvocationARB;
 uint v=id+1u;
 results.values[id*2u]=uvec4(subgroupAdd(v),uint(subgroupAdd(float(v))),subgroupMin(v),subgroupMax(v));
 results.values[id*2u+1u]=uvec4(0u);
 if(lane%3u!=0u) {
  results.values[id*2u+1u]=uvec4(subgroupAdd(v),subgroupMin(v),subgroupMax(v),subgroupBallotBitCount(subgroupBallot(true)));
 }
}
