from pathlib import Path
import sys,gzip,json,math,base64
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_world import encode

def ref(value):return value['$ref']
def main(path):
 source={'p':{'m_FileID':4,'m_PathID':9223372036854775806},'array':[b'\0\1\xff',float('inf'),float('-inf'),float('nan')]}
 encoded=json.loads(json.dumps(encode(source),allow_nan=False))
 assert ref(encoded['p'])=={'file':4,'path':'9223372036854775806'}
 assert base64.b64decode(encoded['array'][0]['$bytes'])==b'\0\1\xff'
 assert [v['$float'] for v in encoded['array'][1:]]==['inf','-inf','nan']
 data=json.loads(gzip.decompress(Path(path).read_bytes()));assert data['format']=='NXWORLD-MIGRATION-1'
 records={o['id']:o for o in data['objects']};assert len(records)==len(data['objects'])
 transforms={i:o for i,o in records.items() if o['type'] in ('Transform','RectTransform')}
 for i,o in records.items():
  tree=o['data']
  if o['type']=='GameObject':
   for item in tree['m_Component']:
    p=ref(item.get('component',item.get('second',{})))
    assert p['file'] or p['path']=='0' or p['path'] in records,('missing component',i,p)
  elif i in transforms:
   p=ref(tree['m_Father']);assert p['file']==0 and (p['path']=='0' or p['path'] in transforms)
   game=ref(tree['m_GameObject']);assert game['file']==0 and game['path'] in records
 seen=set();stack=set()
 def visit(i):
  if i=='0' or i in seen:return
  assert i not in stack,'transform cycle';stack.add(i);visit(ref(transforms[i]['data']['m_Father'])['path']);stack.remove(i);seen.add(i)
 for i in transforms:visit(i)
 print(f'PASS: {len(records)} authored object/component records, {len(transforms)} transforms; all local component/parent owners retained, acyclic hierarchy; exact 64-bit references/bytes/nonfinite-field encoding')

if __name__=='__main__':main(sys.argv[1])
