"""Compile pinned AMD FSR 3.1 FG compute passes for deko3d; not game integration."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
VENDOR=ROOT/'experimental/framegen/vendor/fidelityfx-1.1.3'
PYRAMID=(ROOT/'experimental/framegen/deko/pyramid.glsl').read_text(encoding='utf-8')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def convert(source):
    # Vulkan separate image/sampler descriptors become deko combined handles.
    source=re.sub(r'layout\s*\([^)]*\)\s*uniform\s+sampler\s+\w+\s*;','',source)
    sampling={}
    def combine(m):
        tex,sampler=m.group(1,2)
        if tex in sampling and sampling[tex]!=sampler:raise ValueError('Multiple samplers for '+tex)
        sampling[tex]=sampler
        return tex
    source=re.sub(r'sampler2D\(\s*(\w+)\s*,\s*(\w+)\s*\)',combine,source)
    source=re.sub(r'\b([iu]?)texture2D\b',r'\1sampler2D',source)
    source=re.sub(r'\bset\s*=\s*0\s*,\s*','',source)
    source=re.sub(r'\bpacked\b','ffxPackedValue',source)
    # SPD needs thirteen UAV mip views; deko exposes eight image bindings.
    # Store RGBA16F as packed uvec2 in one coherent SSBO. The backend must
    # copy these linear mip ranges into the sampled pyramid after dispatch.
    has_pyramid='rw_inpainting_pyramid0' in source
    if has_pyramid:
        source=re.sub(r'layout\s*\([^)]*\)\s*(?:coherent\s+)?uniform\s+image2D\s+rw_inpainting_pyramid\d+\s*;','',source)
        source=re.sub(r'imageLoad\(rw_inpainting_pyramid(\d+)\s*,',r'nxPyramidLoad(\1u,',source)
        source=re.sub(r'imageStore\(rw_inpainting_pyramid(\d+)\s*,',r'nxPyramidStore(\1u,',source)
        source=PYRAMID+source
    resources=[];counts={'texture':0,'image':0,'uniform':0,'storage':0}
    def binding(m):
        attrs,qual,typ,name=m.group(1,2,3,4)
        b=re.search(r'\bbinding\s*=\s*(\d+)',attrs)
        if not b:return m.group(0)
        is_buffer='buffer' in qual
        kind='texture' if 'sampler' in typ else 'image' if 'image' in typ else 'storage' if is_buffer else 'uniform'
        slot=counts[kind];counts[kind]+=1
        instance=re.match(r'\s*\{[^}]*\}\s*(\w+)',source[m.end():]) if name is None else None
        resource_name=instance[1] if instance else name or typ
        resources.append({'name':resource_name,'kind':kind,'slot':slot,'source_binding':int(b[1]),'type':typ,'sampler':sampling.get(name)})
        attrs=attrs[:b.start()]+f'binding = {slot}'+attrs[b.end():]
        if kind=='image' and typ=='image2D' and not re.search(r'\br(?:g|gb|gba)?\d',attrs):attrs+=', rgba16f'
        return f'layout({attrs}) {qual} {typ}'+(' '+name if name else '')
    source=re.sub(r'layout\s*\(([^)]*)\)\s*((?:(?:readonly|writeonly|coherent)\s+)*(?:uniform|buffer))\s+(\w+)(?:[ \t]+(\w+))?',binding,source)
    for kind,limit in [('texture',32),('image',8),('uniform',16),('storage',16)]:
        if counts[kind]>limit:raise ValueError(f'{kind} bindings exceed deko limit: {counts[kind]}')
    return source,resources

def build(args):
    provenance=json.loads((VENDOR/'upstream.json').read_text())
    for name,digest in provenance['files'].items():
        if sha(VENDOR/name)!=digest:raise ValueError('Upstream changed: '+name)
    args.output.mkdir(parents=True,exist_ok=True)
    results=[]
    with tempfile.TemporaryDirectory(prefix='nx-fg-') as temp:
        tmp=Path(temp)
        for src in VENDOR.rglob('*'):
            if src.suffix not in ('.h','.glsl'):continue
            dst=tmp/src.relative_to(VENDOR);dst.parent.mkdir(parents=True,exist_ok=True)
            code=re.sub(r'^\s*#\s*(?:version|extension)\b[^\n]*','',src.read_text(),flags=re.M)
            dst.write_text(code)
        for src in sorted((tmp/'passes').rglob('*.glsl')):
            name=src.stem;out=args.output/name
            cpp=subprocess.run([args.cc,'-E','-P','-x','c','-DFFX_GPU=1','-DFFX_GLSL=1','-DFFX_HALF=0',
                 '-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0','-DFFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2',
                 f'-DFFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS={args.low_res_motion_vectors}',
                 f'-DFFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH={int(args.inverted_depth)}',
                 f'-DFFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS={int(args.jitter_motion_vectors)}',
                 '-I'+str(tmp/'gpu'),'-I'+str(tmp/'gpu/opticalflow'),'-I'+str(tmp/'gpu/frameinterpolation'),str(src)],capture_output=True,text=True)
            if cpp.returncode:raise RuntimeError(cpp.stderr)
            code,resources=convert(cpp.stdout)
            extensions='''#version 460
#extension GL_ARB_shader_ballot : require
#extension GL_ARB_gpu_shader_int64 : require
'''
            out.with_suffix('.glsl').write_text(extensions+(ROOT/'experimental/framegen/deko/subgroups.glsl').read_text(encoding='utf-8')+'\n'+code)
            compile=subprocess.run([args.uam,'-s','comp','-o',str(out.with_suffix('.dksh')),str(out.with_suffix('.glsl'))],capture_output=True,text=True)
            log=compile.stdout+compile.stderr;out.with_suffix('.log').write_text(log)
            record={'pass':name,'compiled':compile.returncode==0,'resources':resources,'pyramid_copy_required':any(r['name']=='nxPyramid' for r in resources)}
            if compile.returncode==0:
                blob=out.with_suffix('.dksh').read_bytes()
                magic,header,control,size,programs,count=struct.unpack_from('<6I',blob)
                assert magic==0x48534b44 and header==24 and control+size==len(blob) and count==1
                info=struct.unpack_from('<16I',blob,programs)
                assert info[0]==5 and all(info[i]>0 for i in (6,7,8))
                record.update(sha256=sha(out.with_suffix('.dksh')),bytes=len(blob),workgroup=info[6:9],gprs=info[2],scratch_bytes_per_warp=info[5],shared_memory_bytes=info[9])
            results.append(record)
            print(('PASS ' if record['compiled'] else 'FAIL ')+name,flush=True)
            if not record['compiled']:print(log[:1200],flush=True)
    report={'upstream':{k:v for k,v in provenance.items() if k!='files'},'hardware_verified':False,'runtime_integrated':False,'profile':{'hdr':False,'half':False,'low_res_motion_vectors':args.low_res_motion_vectors,'inverted_depth':args.inverted_depth,'jitter_motion_vectors':args.jitter_motion_vectors},'passes':results}
    (args.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if all(p['compiled'] for p in results) else 1
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');ap.add_argument('--uam',required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--low-res-motion-vectors',type=int,choices=(0,1),default=1);ap.add_argument('--inverted-depth',action='store_true');ap.add_argument('--jitter-motion-vectors',action='store_true')
    raise SystemExit(build(ap.parse_args()))
