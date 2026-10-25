layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D probeInput;
layout(std430,binding=0) buffer ProbeSamples { vec4 pixels[]; };
void main() {
 uvec2 p=gl_GlobalInvocationID.xy;
 pixels[p.y*64u+p.x]=texelFetch(probeInput,ivec2(p),0);
}
