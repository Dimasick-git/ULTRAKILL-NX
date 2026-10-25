"""Reproduce the native Move comparison using the user's TEST16 C++ export."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys

BASELINE='5f3d70a76b5ec7657864b9145b03f9abce206fea066d92d3c8d80560576e5abd'

def main():
 p=argparse.ArgumentParser();p.add_argument('--cpp',required=True,type=Path);p.add_argument('--compiler',required=True,type=Path);p.add_argument('--output',required=True,type=Path);args=p.parse_args()
 root=Path(__file__).resolve().parents[1];args.output.mkdir(parents=True,exist_ok=True)
 reference=args.output/'movement_original.inc'
 subprocess.run([sys.executable,str(root/'tools/extract_movement_reference.py'),str(args.cpp),str(reference)],check=True)
 provenance=json.loads(reference.with_suffix('.json').read_text())
 if provenance['original_method_sha256']!=BASELINE:raise ValueError('Original Move differs from TEST16; audit it before updating the baseline hash')
 binary=args.output/('movement_test.exe' if sys.platform=='win32' else 'movement_test')
 command=[str(args.compiler),'-std=c++17','-O2','-ffp-contract=off','-I',str(root/'include'),'-I',str(root/'tests'),'-I',str(args.output),str(root/'source/nx_movement.cpp'),str(root/'tests/movement_test.cpp'),'-o',str(binary)]
 subprocess.run(command,check=True)
 result=subprocess.run([str(binary)],check=True,text=True,capture_output=True)
 (args.output/'movement-test.log').write_text(result.stdout+result.stderr)
 provenance.update(native_sha256=hashlib.sha256((root/'source/nx_movement.cpp').read_bytes()).hexdigest(),test_sha256=hashlib.sha256((root/'tests/movement_test.cpp').read_bytes()).hexdigest(),result=result.stdout.strip(),scope='NewMovement.Move with mocked engine boundaries; not collision, gameplay integration or performance validation')
 (args.output/'movement-validation.json').write_text(json.dumps(provenance,indent=2)+'\n')
 print(result.stdout,end='')

if __name__=='__main__':main()
