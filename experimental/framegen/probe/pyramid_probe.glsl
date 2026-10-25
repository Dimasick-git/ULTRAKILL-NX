layout(local_size_x=32) in;
layout(std430,binding=4002) buffer Results { vec4 values[]; } results;
void main(){
 uint m=gl_LocalInvocationIndex;
 if(m>=13u)return;
 vec4 v=vec4(float(m),float(m)+0.5,-float(m),1.0);
 nxPyramidStore(m,ivec2(-1,-1),vec4(99.0));
 nxPyramidStore(m,ivec2(4,0),vec4(99.0));
 nxPyramidStore(m,ivec2(1,1),v);
 memoryBarrierBuffer();
 results.values[m]=nxPyramidLoad(m,ivec2(1,1));
}
