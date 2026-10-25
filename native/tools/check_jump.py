"""Reproduce Jump differential validation against the pinned local export."""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--cpp',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True);reference=a.output/'jump_original.inc'
 subprocess.run([sys.executable,str(root/'tools/extract_jump_reference.py'),str(a.cpp),str(reference)],check=True)
 exe=a.output/('jump_differential_test.exe' if sys.platform=='win32' else 'jump_differential_test')
 subprocess.run([str(a.compiler),'-std=c++17','-O2','-ffp-contract=off','-I',str(root/'include'),'-I',str(root/'tests'),'-I',str(a.output),str(root/'source/nx_jump.cpp'),str(root/'tests/jump_differential_test.cpp'),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],check=True,capture_output=True,text=True);print(result.stdout,end='')
 report=json.loads(reference.with_suffix('.json').read_text());report.update(native_sha256=hashlib.sha256((root/'source/nx_jump.cpp').read_bytes()).hexdigest(),test_sha256=hashlib.sha256((root/'tests/jump_differential_test.cpp').read_bytes()).hexdigest(),result=result.stdout.strip())
 (a.output/'jump-validation.json').write_text(json.dumps(report,indent=2)+'\n');(a.output/'jump-test.log').write_text(result.stdout+result.stderr)
if __name__=='__main__':main()
