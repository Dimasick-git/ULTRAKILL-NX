import sys,unittest
from pathlib import Path
from types import SimpleNamespace as NS
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_render import vertex_color

class VertexColors(unittest.TestCase):
 def fixture(self,fmt,rgba):
  mesh=NS(m_VertexData=NS(m_Channels=[NS(dimension=0)]*3+[NS(format=fmt,dimension=4)]),m_CompressedMesh=NS(m_Colors=NS(m_NumItems=0)))
  return mesh,NS(m_Colors=[rgba])
 def test_byte_values_decode_by_declared_format(self):
  self.assertEqual(vertex_color(*self.fixture(2,(255,128,1,255)),0),0xff0180ff)
  self.assertEqual(vertex_color(*self.fixture(2,(1,0,0,1)),0),0x01000001)
 def test_float_colors_and_absent_colors(self):
  self.assertEqual(vertex_color(*self.fixture(0,(1,.5,0,1)),0),0xff0080ff)
  m,h=self.fixture(0,());h.m_Colors=None;self.assertEqual(vertex_color(m,h,0),0xffffffff)
 def test_unsupported_and_nonfinite_rejected(self):
  with self.assertRaises(ValueError):vertex_color(*self.fixture(7,(1,1,1,1)),0)
  with self.assertRaises(ValueError):vertex_color(*self.fixture(0,(float('nan'),1,1,1)),0)

if __name__=='__main__':unittest.main()
