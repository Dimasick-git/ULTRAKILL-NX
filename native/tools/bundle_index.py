"""Index serialized-file names without decompressing UnityFS asset payloads.

The supported input is modern, unencrypted UnityFS (header version >= 7).
UnityPy supplies the block-info codecs, not asset discovery heuristics.
"""
from pathlib import Path
import io,struct

def read_exact(stream,n):
 data=stream.read(n)
 if len(data)!=n:raise ValueError('truncated UnityFS directory')
 return data

def cstring(stream):
 value=bytearray()
 for _ in range(4096):
  byte=read_exact(stream,1)
  if byte==b'\0':return value.decode('utf-8',errors='strict')
  value+=byte
 raise ValueError('UnityFS directory string budget')

def directory(path):
 from UnityPy.helpers.CompressionHelper import decompress_lz4,decompress_lzma
 with Path(path).open('rb') as stream:
  if cstring(stream)!='UnityFS':raise ValueError('unsupported asset bundle signature')
  version=struct.unpack('>I',read_exact(stream,4))[0]
  if version<7:raise ValueError('unsupported old UnityFS header')
  cstring(stream);cstring(stream)
  size,compressed,expanded,flags=struct.unpack('>QIII',read_exact(stream,20))
  if size!=Path(path).stat().st_size or not 0<compressed<=16*1024*1024 or not 0<expanded<=16*1024*1024:raise ValueError('UnityFS header length/budget')
  if flags&~0x2ff or flags&0x200:raise ValueError('unsupported UnityFS flags')
  stream.seek((stream.tell()+15)&~15)
  if flags&0x80:stream.seek(size-compressed)
  encoded=read_exact(stream,compressed);codec=flags&0x3f
  if codec==0:data=encoded
  elif codec==1:data=decompress_lzma(encoded)
  elif codec in (2,3):data=decompress_lz4(encoded,expanded)
  else:raise ValueError('unsupported UnityFS info codec')
  if len(data)!=expanded:raise ValueError('UnityFS info length mismatch')
  info=io.BytesIO(data);read_exact(info,16)
  blocks=struct.unpack('>I',read_exact(info,4))[0]
  if blocks>1000000:raise ValueError('UnityFS block count budget')
  logical=0
  for _ in range(blocks):
   unpacked,packed,blockflags=struct.unpack('>IIH',read_exact(info,10));logical+=unpacked
  count=struct.unpack('>I',read_exact(info,4))[0]
  if count>65536:raise ValueError('UnityFS directory entry budget')
  names=[]
  for _ in range(count):
   offset,length,entryflags=struct.unpack('>QQI',read_exact(info,20));name=cstring(info)
   if offset+length>logical:raise ValueError('UnityFS entry outside payload')
   names.append(name)
  if info.read(1):raise ValueError('trailing UnityFS info bytes')
  return names

def build_index(root):
 result={}
 for path in sorted(Path(root).rglob('*.bundle')):
  for name in directory(path):
   key=name.lower()
   if key in result and result[key]!=path:raise ValueError('ambiguous serialized asset name: '+name)
   result[key]=path
 return result

class Resolver:
 """Load only bundles required by serialized-file references; never guess IDs."""
 def __init__(self,env,root):
  self.env=env;self.index=build_index(root);self.loaded=set();self.dependencies=set()
  self.defaults=Path(root).parents[1]/'bin/Data/unity default resources'
 def resolve(self,pointer):
  if pointer.m_PathID==0:return None
  file=pointer.assetsfile
  if pointer.m_FileID:
   external=file.externals[pointer.m_FileID-1].path.replace('\\','/').split('/')[-1].lower()
   if external=='unity default resources':path=self.defaults
   else:
    path=self.index.get(external)
    if path is None:raise ValueError('unresolved serialized file: '+external)
   if path not in self.loaded:
    self.env.load_file(str(path));self.loaded.add(path);self.dependencies.add(path)
  return pointer.deref()
