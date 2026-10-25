"""Compare native revolver control with the local archived r52 Update body."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
def main():
 p=argparse.ArgumentParser();p.add_argument('--cpp',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
 (a.output/'revolver-validation.json').write_text(json.dumps({'status':'running','hardware_verified':False,'full_combat':False})+'\n')
 reference=a.output/'revolver_original.inc';subprocess.run([sys.executable,str(root/'tools/extract_revolver_reference.py'),str(a.cpp),str(reference)],check=True)
 exe=a.output/('revolver_differential_test.exe' if sys.platform=='win32' else 'revolver_differential_test');subprocess.run([str(a.compiler),'-std=c++17','-O2','-ffp-contract=off','-I',str(root/'include'),'-I',str(root/'tests'),'-I',str(a.output),str(root/'source/nx_revolver.cpp'),str(root/'tests/revolver_differential_test.cpp'),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True);(a.output/'revolver-test.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr);result.check_returncode()
 report=json.loads(reference.with_suffix('.json').read_text());report.update(status='host_passed',result=result.stdout.strip(),native_sha256=hashlib.sha256((root/'source/nx_revolver.cpp').read_bytes()).hexdigest(),test_sha256=hashlib.sha256((root/'tests/revolver_differential_test.cpp').read_bytes()).hexdigest(),hardware_verified=False,full_combat=False);(a.output/'revolver-validation.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
