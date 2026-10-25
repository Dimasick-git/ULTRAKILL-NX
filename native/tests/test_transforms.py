from pathlib import Path
import sys,math
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_scene import trs,mul,point,normal_matrix,identity,batch_matrix

def close(a,b):assert all(abs(x-y)<1e-5 for x,y in zip(a,b)),(a,b)
def transform(position,rotation,scale):return trs({'m_LocalPosition':dict(zip('xyz',position)),'m_LocalRotation':dict(zip('xyzw',rotation)),'m_LocalScale':dict(zip('xyz',scale))})
parent=transform((10,20,30),(0,math.sin(math.pi/4),0,math.cos(math.pi/4)),(2,3,4))
child=transform((1,2,3),(0,0,0,1),(1,1,1))
close(point(mul(parent,child),(0,0,0)),(22,26,28))
normal=(1,1,0);tangent=(1,-1,0);nm=normal_matrix(parent)
n=[sum(nm[r][c]*normal[c] for c in range(3)) for r in range(3)]
t=[sum(parent[r][c]*tangent[c] for c in range(3)) for r in range(3)]
assert abs(sum(a*b for a,b in zip(n,t)))<1e-6
negative=transform((0,0,0),(0,0,0,1),(-2,3,4));close(point(negative,(1,1,1)),(-2,3,4))
try:normal_matrix(transform((0,0,0),(0,0,0,1),(0,1,1)))
except ValueError:pass
else:raise AssertionError('singular normal matrix accepted')
worlds={0:identity(),1:parent,2:child}
close(point(batch_matrix(1,0,1,worlds.__getitem__),(4,5,6)),(4,5,6))
close(point(batch_matrix(1,2,1,worlds.__getitem__),(4,5,6)),(5,7,9))
close(point(batch_matrix(0,0,2,worlds.__getitem__),(4,5,6)),(5,7,9))
print('PASS: hierarchy rotation/translation/nonuniform scale, inverse-transpose normals, negative scale and singular transform rejection')
