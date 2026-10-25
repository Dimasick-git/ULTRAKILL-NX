"""Check descriptor/ABI adaptation contracts, including rejecting unsupported layouts."""
from pathlib import Path
import hashlib,json
from build_fsr31_deko import convert,VENDOR

def main():
 provenance=json.loads((VENDOR/'upstream.json').read_text())
 assert provenance['commit']=='54fbaafdc34716811751bea5032700e78f5a0f33'
 for n,digest in provenance['files'].items():assert hashlib.sha256((VENDOR/n).read_bytes()).hexdigest()==digest,n
 code='''layout(set=0,binding=1000) uniform sampler linearClamp;
layout(set=0,binding=11) uniform texture2D color;
layout(set=0,binding=12,rgba16f) coherent uniform image2D outputImage;
layout(set=0,binding=13,std140) uniform Params { vec4 dimensions; } params;
layout(set=0,binding=14,std430) readonly buffer Input { uint data[]; } inputData;
vec4 f(vec2 uv){return textureLod(sampler2D(color,linearClamp),uv,0.0);}
'''
 fixed,resources=convert(code);byname={r['name']:r for r in resources}
 assert set(byname)=={'color','outputImage','params','inputData'}
 for name,kind in [('color','texture'),('outputImage','image'),('params','uniform'),('inputData','storage')]:assert byname[name]['kind']==kind and byname[name]['slot']==0
 assert byname['color']['sampler']=='linearClamp' and 'set=' not in fixed and 'uniform texture2D' not in fixed
 try:convert(code+'vec4 g(vec2 uv){return textureLod(sampler2D(color,otherSampler),uv,0.0);}')
 except ValueError:pass
 else:raise AssertionError('Unsupported multi-sampler alias must fail closed')
 try:convert('\n'.join(f'layout(binding={i},r32f) uniform image2D r{i};' for i in range(9)))
 except ValueError:pass
 else:raise AssertionError('deko image binding limit not enforced')
 declarations='\n'.join(f'layout(set=0,binding={i},rgba16f) coherent uniform image2D rw_inpainting_pyramid{i};' for i in range(13))
 fixed,resources=convert(declarations+'void f(){imageStore(rw_inpainting_pyramid12,ivec2(0),vec4(1));}')
 assert 'rw_inpainting_pyramid' not in fixed and 'nxPyramidStore(12u,' in fixed
 assert {r['name'] for r in resources}=={'nxPyramid','nxPyramidLayout'}
 print('PASS: pinned AMD provenance, dense descriptor namespaces, reflected block names, sampler semantics, binding limits, 13-mip storage adaptation')
if __name__=='__main__':main()
