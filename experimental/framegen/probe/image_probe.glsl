layout(local_size_x=8,local_size_y=8) in;
layout(binding=0,r8ui) uniform writeonly uimage2D probeOutput;
void main() {
 uvec2 p=gl_GlobalInvocationID.xy;
 imageStore(probeOutput,ivec2(p),uvec4((p.x+3u*p.y)&255u,0,0,0));
}
