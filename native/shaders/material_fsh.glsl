#version 460
layout(location=0) in vec4 vertexColor;
layout(location=1) in vec2 textureUv;
layout(std140,binding=0) uniform Material {
 vec4 uv;
 vec4 tint;
 vec4 emissive;
 vec4 parameters;
 vec4 emissiveUv;
} material;
layout(binding=0) uniform sampler2D baseTexture;
layout(binding=1) uniform sampler2D emissiveTexture;
layout(location=0) out vec4 outputColor;
void main(){
 vec4 albedo=texture(baseTexture,textureUv*material.uv.xy+material.uv.zw)*material.tint;
 albedo*=mix(vec4(1.0),vertexColor,clamp(material.parameters.y,0.0,1.0));
 vec3 emission=texture(emissiveTexture,textureUv*material.emissiveUv.xy+material.emissiveUv.zw).rgb*material.emissive.rgb*material.parameters.z;
 outputColor=vec4(albedo.rgb+emission,albedo.a*material.parameters.w);
}
