#version 460
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(std140,binding=0) uniform Camera {mat4 viewProjection;} camera;
layout(location=0) out vec4 shadedColor;
void main(){
 gl_Position=camera.viewProjection*vec4(position,1.0);
 float light=0.35+0.65*abs(dot(normalize(normal),normalize(vec3(0.4,0.8,0.3))));
 shadedColor=vec4(color.rgb*light,color.a);
}
