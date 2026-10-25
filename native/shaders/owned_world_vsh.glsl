#version 460
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(std140,binding=0) uniform Model {
 mat4 modelViewProjection;
 mat4 normalWorld;
} model;
layout(location=0) out vec4 shadedColor;
layout(location=1) out vec2 textureUv;
void main(){
 gl_Position=model.modelViewProjection*vec4(position,1.0);
 shadedColor=color;
 textureUv=uv;
}
