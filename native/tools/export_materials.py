"""Export exact material properties and native base/emissive texture data.

This is a resource conversion stage, not a claim of original shader parity.
The sidecar retains every material property and unsupported texture reference.
RGBA8 pixels keep Unity's native UV row order (no image-preview Y flip).
"""
from pathlib import Path
import argparse,hashlib,json,math,struct,sys,zlib
from bundle_index import Resolver

def lossless(value):
 if isinstance(value,float) and not math.isfinite(value):return {'nonfinite':repr(value)}
 if isinstance(value,int) and abs(value)>2**53:return str(value)
 if isinstance(value,dict):return {k:lossless(v) for k,v in value.items()}
 if isinstance(value,(list,tuple)):return [lossless(v) for v in value]
 return value

def export(a):
 if a.python_lib:sys.path.insert(0,str(a.python_lib))
 import UnityPy
 from UnityPy.classes.PPtr import PPtr
 from UnityPy.export.Texture2DConverter import get_image_from_texture2d
 data=a.render.read_bytes();header=struct.unpack_from('<8s6I6f2I',data)
 if header[0]!=b'NXRENDER' or header[1]!=1 or zlib.crc32(data[64:])!=header[-2]:raise ValueError('invalid owned render pack')
 start=64+header[2]*36+header[3]*4+header[5]*32
 refs=sorted({struct.unpack_from('<Iq',data,start+i*48+4) for i in range(header[4])})
 env=UnityPy.load(str(a.bundle));scene=next(o.assets_file for o in env.objects if o.type.name=='MeshRenderer');resolver=Resolver(env,a.bundle.parent)
 texture_ids={};texture_records=[];pixels=bytearray();material_records=[];materials=[];textures=[];shader_cache={};pending=[]
 def key(o):return o.assets_file.name.lower(),o.path_id
 def pointer(value,source):return PPtr(m_FileID=value.get('m_FileID',0),m_PathID=value['m_PathID'],assetsfile=source)
 def texture(value,source):
  if not value['m_PathID']:return 0xffffffff
  obj=resolver.resolve(pointer(value,source));identity=key(obj)
  if identity in texture_ids:return texture_ids[identity]
  if obj.type.name!='Texture2D':
   pending.append({'texture':identity,'type':obj.type.name});return 0xffffffff
  tex=obj.read();w,h=tex.m_Width,tex.m_Height
  if not 0<w<=8192 or not 0<h<=8192:raise ValueError('texture dimension budget')
  image=get_image_from_texture2d(tex,flip=False).convert('RGBA')
  if image.size!=(w,h):raise ValueError('decoded texture dimensions differ')
  raw=image.tobytes()
  if len(pixels)+len(raw)>256*1024*1024:raise ValueError('native texture pixel budget')
  settings=tex.m_TextureSettings;wrapu=settings.m_WrapU;wrapv=settings.m_WrapV;filtermode=settings.m_FilterMode
  if wrapu not in range(4) or wrapv not in range(4) or filtermode not in range(3):raise ValueError('unsupported texture sampler')
  index=len(texture_records);texture_ids[identity]=index
  texture_records.append(struct.pack('<8Iq6I',w,h,len(pixels),len(raw),wrapu,wrapv,filtermode,tex.m_ColorSpace,obj.path_id,zlib.crc32(identity[0].encode()),0,0,0,0,0));pixels.extend(raw)
  textures.append({'index':index,'source':identity,'name':tex.m_Name,'width':w,'height':h,'source_format':int(tex.m_TextureFormat),'source_mips':tex.m_MipCount,'native_mips':1,'row_order':'Unity texture data; not preview-flipped','rgba_sha256':hashlib.sha256(raw).hexdigest(),'sampler':{'wrap_u':wrapu,'wrap_v':wrapv,'filter':filtermode,'color_space':tex.m_ColorSpace}})
  return index
 def uv(tex):return [tex['m_Scale'][c] for c in ('x','y')]+[tex['m_Offset'][c] for c in ('x','y')]
 for file_id,path_id in refs:
  if not path_id:
   if file_id:raise ValueError('noncanonical null material reference')
   material_records.append(struct.pack('<IIq4I20f',0,0,0,0xffffffff,0xffffffff,1,0,*([1,1,0,0]+[1,0,1,1]+[0,0,0,1]+[.5,0,0,1]+[1,1,0,0])))
   materials.append({'file':0,'id':'0','name':'[source null material]','source':None,'shader':None,'properties':None,'native_fallback':'magenta diagnostic, original renderer null-material behavior needs visual validation','native_shader_complete':False});continue
  obj=resolver.resolve(PPtr(m_FileID=file_id,m_PathID=path_id,assetsfile=scene))
  if obj.type.name!='Material':raise ValueError('material reference type mismatch')
  tree=obj.read_typetree();sp=resolver.resolve(pointer(tree['m_Shader'],obj.assets_file));sk=key(sp)
  if sk not in shader_cache:shader_cache[sk]=sp.read().m_ParsedForm.m_Name
  shader=shader_cache[sk];props=tree['m_SavedProperties'];envs=dict(props['m_TexEnvs']);floats=dict(props['m_Floats']);floats.update(props.get('m_Ints',[]));colors=dict(props['m_Colors'])
  main=envs.get('_MainTex',{'m_Texture':{'m_PathID':0},'m_Scale':{'x':1,'y':1},'m_Offset':{'x':0,'y':0}});emission=envs.get('_EmissiveTex',envs.get('_EmissionMap',main if floats.get('_UseAlbedoAsEmissive',0) else {'m_Texture':{'m_PathID':0},'m_Scale':{'x':1,'y':1},'m_Offset':{'x':0,'y':0}}))
  tint=colors.get('_Color',dict(r=1,g=1,b=1,a=1));ec=colors.get('_EmissiveColor',colors.get('_EmissionColor',dict(r=0,g=0,b=0,a=1)))
  values=uv(main)+[tint[c] for c in 'rgba']+[ec[c] for c in 'rgba']+[floats.get('_Cutoff',.5),floats.get('_VertexColors',0),floats.get('_EmissiveIntensity',1) if floats.get('EMISSIVE',0) else 0,floats.get('_Opacity',1)]+uv(emission)
  if not all(math.isfinite(v) for v in values):raise ValueError('nonfinite runtime material value')
  shader_kind={'ULTRAKILL/Master':1,'psx/unlit/ambient':2,'ULTRAKILL/Additive':3,'psx/vertexlit/vertexlit':4}.get(shader,0)
  flags=(1 if floats.get('_ZWrite',1) else 0)|(2 if shader_kind==3 else 0)
  material_records.append(struct.pack('<IIq4I20f',file_id,shader_kind,path_id,texture(main['m_Texture'],obj.assets_file),texture(emission['m_Texture'],obj.assets_file),flags,0,*values))
  materials.append({'file':file_id,'id':str(path_id),'source':key(obj),'shader_source':sk,'shader':shader,'properties':tree,'native_shader_complete':False})
  print('Material',len(materials),'/',len(refs),tree['m_Name'],flush=True)
 payload=b''.join(material_records)+b''.join(texture_records)+pixels
 result=struct.pack('<8s6I',b'NXMAT\0\0\0',1,len(material_records),len(texture_records),len(pixels),zlib.crc32(payload),header[-2])+payload
 a.output.parent.mkdir(parents=True,exist_ok=True);temp=a.output.with_name(a.output.name+'.tmp');temp.write_bytes(result);temp.replace(a.output)
 report={'format':'NXMAT1','source_sha256':hashlib.sha256(a.bundle.read_bytes()).hexdigest(),'render_sha256':hashlib.sha256(data).hexdigest(),'output_sha256':hashlib.sha256(result).hexdigest(),'bytes':len(result),'material_count':len(materials),'texture_count':len(textures),'pixel_bytes':len(pixels),'materials':materials,'textures':textures,'pending_texture_types':pending,'dependencies':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(resolver.dependencies)},'shader_parity':False,'runtime_connected':False}
 a.output.with_suffix('.materials.json').write_text(json.dumps(lossless(report),ensure_ascii=False,allow_nan=False,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items() if k not in ('materials','textures','dependencies')},indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--render',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-lib',type=Path);export(p.parse_args())
