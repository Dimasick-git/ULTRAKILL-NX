import copy,struct,sys,unittest,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_graph import convert

def ref(path,file=0):return {'$ref':{'file':file,'path':str(path)}}
def fixture():
 objects=[]
 for go,transform,parent,active in [(10,11,0,True),(20,21,11,False)]:
  components=[{'component':ref(transform)}]
  if go==20:components.append({'component':ref(22)})
  objects.append({'id':str(go),'type':'GameObject','data':{'m_Name':'Room' if go==10 else 'Кнопка','m_Component':components,'m_IsActive':active,'m_Layer':0,'m_Tag':0}})
  objects.append({'id':str(transform),'type':'Transform','data':{'m_GameObject':ref(go),'m_Father':ref(parent),'m_LocalPosition':dict(x=0,y=1,z=0),'m_LocalRotation':dict(x=0,y=0,z=0,w=1),'m_LocalScale':dict(x=1,y=1,z=1)}})
 objects.append({'id':'22','type':'MonoBehaviour','data':{'m_GameObject':ref(20),'m_Enabled':0},'script':{'assembly':'Assembly-CSharp.dll','namespace':'A','class':'Door'}})
 objects.append({'id':'99','type':'MonoBehaviour','data':{'m_GameObject':ref(0),'m_Enabled':1},'script':None})
 return {'format':'NXWORLD-MIGRATION-1','bundle_sha256':'local-fixture','objects':objects}

class ExportTests(unittest.TestCase):
 def test_roundtrip_ownership_and_identity(self):
  world=fixture();packed,report=convert(world);self.assertEqual(packed,convert(world)[0]);self.assertEqual(report['nodes'],2);self.assertEqual(report['components'],3);self.assertEqual(report['active_in_hierarchy'],1);self.assertEqual(report['unbound_records_retained_in_world_snapshot'],1)
  magic,version,nodes,components,strings,crc,reserved=struct.unpack_from('<8s6I',packed);self.assertEqual((magic,version,reserved),(b'NXGRAPH\0',1,0));self.assertEqual(crc,zlib.crc32(packed[32:]));self.assertEqual(len(packed),32+88*nodes+32*components+strings)
  table=packed[32+88*nodes+32*components:].decode('utf-8');self.assertIn('Кнопка',table);self.assertIn('Assembly-CSharp.dll::A.Door',table)
 def test_inherited_activity(self):
  world=fixture();world['objects'][0]['data']['m_IsActive']=False;world['objects'][2]['data']['m_IsActive']=True;self.assertEqual(convert(world)[1]['active_in_hierarchy'],0)
 def test_rejects_malformed_graph(self):
  cases=[];world=fixture();world['objects'][1]['data']['m_Father']=ref(21);cases.append(world)
  world=fixture();world['objects'][3]['data']['m_GameObject']=ref(10);cases.append(world)
  world=fixture();world['objects'][2]['data']['m_Component'].append({'component':ref(22)});cases.append(world)
  world=fixture();world['objects'][2]['data']['m_Component'][0]['component']=ref(21,1);cases.append(world)
  world=fixture();world['objects'][1]['data']['m_LocalRotation']['w']=0;cases.append(world)
  world=fixture();world['objects'][0]['data']['m_Layer']=32;cases.append(world)
  world=fixture();world['objects'].append(copy.deepcopy(world['objects'][0]));cases.append(world)
  for world in cases:
   with self.subTest(world=world):
    with self.assertRaises(ValueError):convert(world)

if __name__=='__main__':unittest.main()
