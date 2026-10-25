import sys,tempfile,struct,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bundle_index import directory

class BundleDirectory(unittest.TestCase):
 def fixture(self,end=False):
  info=b'\0'*16+struct.pack('>I',1)+struct.pack('>IIH',8,8,0)+struct.pack('>I',1)+struct.pack('>QQI',0,8,4)+b'CAB-fixture\0'
  prefix=b'UnityFS\0'+struct.pack('>I',8)+b'5.x.x\0' +b'2022.3.0f1\0';prefix+=struct.pack('>QIII',0,len(info),len(info),0xc0 if end else 0x40);prefix+=b'\0'*(-len(prefix)%16)
  data=bytearray(prefix+(b'payload!' +info if end else info+b'payload!'));struct.pack_into('>Q',data,len(b'UnityFS\0')+4+len(b'5.x.x\0')+len(b'2022.3.0f1\0'),len(data));return data
 def read(self,data):
  with tempfile.TemporaryDirectory() as folder:
   path=Path(folder)/'test.bundle';path.write_bytes(data);return directory(path)
 def test_start_and_end_directory(self):
  for end in (False,True):self.assertEqual(self.read(self.fixture(end)),['CAB-fixture'])
 def test_lengths_and_truncation(self):
  with self.assertRaises(ValueError):self.read(self.fixture()[:-1])
  data=self.fixture();data[-len(b'payload!')-len(b'CAB-fixture\0')-16:-len(b'payload!')-len(b'CAB-fixture\0')-8]=struct.pack('>Q',99)
  with self.assertRaises(ValueError):self.read(data)

if __name__=='__main__':unittest.main()
