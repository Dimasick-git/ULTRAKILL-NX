"""Read the game's Addressables JSON location table without loading Unity.

Layout follows the locally supplied ContentCatalogData/SerializationUtilities.
No managed types or JSON object keys are instantiated during conversion.
"""
from pathlib import Path
import base64,json,struct

class Catalog:
 def __init__(self,path):
  self.path=Path(path);self.data=json.loads(self.path.read_text(encoding='utf-8-sig'));c=self.data
  def decode(name):
   raw=base64.b64decode(c[name],validate=True)
   if len(raw)>64*1024*1024:raise ValueError('catalog allocation budget')
   return raw
  keydata=decode('m_KeyDataString');buckets=decode('m_BucketDataString');entries=decode('m_EntryDataString')
  count=struct.unpack_from('<i',buckets)[0]
  if not 0<=count<=1000000 or struct.unpack_from('<i',keydata)[0]!=count:raise ValueError('catalog key count mismatch/budget')
  self.keys=[];self.buckets=[];self.lookup={};pos=4
  for _ in range(count):
   offset,length=struct.unpack_from('<ii',buckets,pos);pos+=8
   if not 0<=length<=1000000 or pos+length*4>len(buckets):raise ValueError('catalog bucket range')
   refs=struct.unpack_from('<'+'i'*length,buckets,pos);pos+=length*4;key=self._key(keydata,offset)
   if key in self.lookup:raise ValueError('duplicate catalog key')
   self.lookup[key]=len(self.keys);self.keys.append(key);self.buckets.append(refs)
  if pos!=len(buckets):raise ValueError('catalog bucket trailing data')
  n=struct.unpack_from('<i',entries)[0]
  if not 0<=n<=1000000 or len(entries)!=4+n*28:raise ValueError('catalog entry length/budget')
  self.entries=[struct.unpack_from('<7i',entries,4+i*28) for i in range(n)]
  for bucket in self.buckets:
   if any(not 0<=i<n for i in bucket):raise ValueError('catalog bucket entry range')
  for internal,provider,dependency,_,extra,primary,type_id in self.entries:
   if not 0<=internal<len(c['m_InternalIds']) or not 0<=provider<len(c['m_ProviderIds']) or dependency<-1 or dependency>=count or not 0<=primary<count or not 0<=type_id<len(c['m_resourceTypes']):raise ValueError('catalog entry reference range')
 def _key(self,data,at):
  if not 4<=at<len(data):raise ValueError('catalog key offset')
  kind=data[at];at+=1
  if kind in (0,1):
   length=struct.unpack_from('<i',data,at)[0];at+=4
   if not 0<=length<=len(data)-at:raise ValueError('catalog string key range')
   return 'string',data[at:at+length].decode('ascii' if kind==0 else 'utf-16-le')
  if kind in (2,3,4):return kind,struct.unpack_from({2:'<H',3:'<I',4:'<i'}[kind],data,at)[0]
  if kind in (5,6):
   length=data[at];at+=1
   if at+length>len(data):raise ValueError('catalog hash/type key range')
   return kind,data[at:at+length].decode('ascii')
  raise ValueError('unsupported catalog key type: '+str(kind))
 def locate(self,key):
  index=self.lookup.get(('string',key))
  return [] if index is None else list(self.buckets[index])
 def internal(self,index):
  text=self.data['m_InternalIds'][self.entries[index][0]];prefixes=self.data.get('m_InternalIdPrefixes',[])
  if prefixes and '#' in text:
   prefix,tail=text.split('#',1)
   if not prefix.isdigit() or int(prefix)>=len(prefixes):raise ValueError('catalog internal prefix range')
   text=prefixes[int(prefix)]+tail
  return text
 def dependencies(self,index):
  dependency=self.entries[index][2];return [] if dependency<0 else list(self.buckets[dependency])
 def bundle_path(self,index):
  text=self.internal(index);prefix='{UnityEngine.AddressableAssets.Addressables.RuntimePath}/'
  if not text.startswith(prefix) or not text.endswith('.bundle'):raise ValueError('catalog location is not a local bundle')
  root=self.path.parent.resolve();path=(root/text[len(prefix):].replace('\\','/')).resolve()
  if not path.is_relative_to(root) or not path.is_file():raise ValueError('catalog bundle path missing/outside root')
  return path
