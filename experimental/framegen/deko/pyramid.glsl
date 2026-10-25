// RGBA16F SPD mip storage adapter. Level entries: pixel offset, width, height, 0.
layout(std430,binding=4000) coherent buffer NxPyramidData { uvec2 pixels[]; } nxPyramid;
layout(std140,binding=4001) uniform NxPyramidLayout { uvec4 levels[13]; } nxPyramidLayout;
vec4 nxPyramidLoad(uint mip,ivec2 pos) {
 uvec4 level=nxPyramidLayout.levels[mip];
 if(any(lessThan(pos,ivec2(0)))||any(greaterThanEqual(uvec2(pos),level.yz))) return vec4(0.0);
 uvec2 p=nxPyramid.pixels[level.x+uint(pos.y)*level.y+uint(pos.x)];
 return vec4(unpackHalf2x16(p.x),unpackHalf2x16(p.y));
}
void nxPyramidStore(uint mip,ivec2 pos,vec4 v) {
 uvec4 level=nxPyramidLayout.levels[mip];
 if(any(lessThan(pos,ivec2(0)))||any(greaterThanEqual(uvec2(pos),level.yz))) return;
 nxPyramid.pixels[level.x+uint(pos.y)*level.y+uint(pos.x)]=uvec2(packHalf2x16(v.xy),packHalf2x16(v.zw));
}
