"""Compare actual archived TryDash/Dodge bodies with native calculations."""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--cpp',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True);reports=[]
 for method,stem in [('TryDash','dash'),('Dodge','dodge'),('StopSlide','stopslide')]:
  reference=a.output/(stem+'_original.inc')
  subprocess.run([sys.executable,str(root/'tools/extract_jump_reference.py'),str(a.cpp),str(reference),'--method',method],check=True)
  exe=a.output/(stem+'_differential_test'+('.exe' if sys.platform=='win32' else ''))
  source=root/('tests/'+stem+'_differential_test.cpp')
  subprocess.run([str(a.compiler),'-std=c++17','-O2','-ffp-contract=off','-I',str(root/'include'),'-I',str(root/'tests'),'-I',str(a.output),str(root/'source/nx_jump.cpp'),str(root/'source/nx_dash.cpp'),str(source),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],check=True,capture_output=True,text=True);print(result.stdout,end='')
  report=json.loads(reference.with_suffix('.json').read_text());report.update(method=method,native_sha256=hashlib.sha256((root/'source/nx_dash.cpp').read_bytes()).hexdigest(),test_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),result=result.stdout.strip());reports.append(report)
  (a.output/(stem+'-test.log')).write_text(result.stdout+result.stderr)
 (a.output/'dash-validation.json').write_text(json.dumps(reports,indent=2)+'\n')
if __name__=='__main__':main()
