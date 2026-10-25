"""Extract the actual IL2CPP Move body for host differential tests.

Generated reference stays in the external build folder, not in this repository.
Only names/runtime metadata and engine boundaries are adapted; the method's
branches, instructions and constants are retained. Unknown calls fail to build.
"""
import argparse,hashlib,json,re
from pathlib import Path

def extract(text):
 match=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void NewMovement_Move_m[0-9A-F]+[^\n]*\n\{',text,re.M)
 if not match:raise ValueError('Move definition not found')
 start=match.end()-1;depth=0
 for at in range(start,len(text)):
  depth+=(text[at]=='{')-(text[at]=='}')
  if depth==0:return text[start:at+1]
 raise ValueError('unbalanced Move body')

def main():
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
 raw=args.source.read_bytes();body=extract(raw.decode('utf-8-sig').replace('\r\n','\n'))
 body_hash=hashlib.sha256(body.encode()).hexdigest()
 metadata=r'\n\s*static bool s_Il2CppMethodInitialized;\s*if \(!s_Il2CppMethodInitialized\)\s*\{.*?s_Il2CppMethodInitialized = true;\s*\}'
 body,count=re.subn(metadata,'',body,flags=re.S);assert count==1
 body=re.sub(r'^\s*il2cpp_codegen_runtime_class_init_inline\([^;]+;','',body,flags=re.M)
 body=re.sub(r'\bNullCheck\([^;]+;','',body)
 types={'Vector3':'Vec3','NewMovement':'Movement','Rigidbody':'Body','GameObject':'GameObject','Transform':'Transform','CustomGroundProperties':'Ground','GroundCheckGroup':'GroundCheck','Quaternion':'Rotation'}
 for name,replacement in types.items():body=re.sub(r'\b'+name+r'_t[0-9A-F]+\b',replacement,body)
 # Distinguish AddForce overloads before removing generated suffixes.
 body=body.replace('Rigidbody_AddForce_m7A3EEEED21F986917107CBA6CC0106DCBC212198','Rigidbody_AddDefaultForce')
 body=body.replace('Rigidbody_AddForce_mBDBC288D0E266BC1B62E3649B4FCE46E7EA9CCBC','Rigidbody_AddModeForce')
 body=re.sub(r'_m[0-9A-F]{40}(?:_inline)?\b','',body)
 body=body.replace('->___','->').replace('.___','.')
 body=body.replace('il2cpp_codegen_multiply','mul').replace('il2cpp_codegen_add','add').replace('il2cpp_codegen_subtract','sub')
 if re.search(r'il2cpp|_t[0-9A-F]{40}|_m[0-9A-F]{40}',body):raise ValueError('unhandled runtime dependency')
 args.output.parent.mkdir(parents=True,exist_ok=True)
 args.output.write_text('// Generated from local game; do not publish.\nnamespace reference {\ninline void originalMove(Movement* __this)\n'+body+'\n}\n')
 args.output.with_suffix('.json').write_text(json.dumps({'source':str(args.source),'source_sha256':hashlib.sha256(raw).hexdigest(),'original_method_sha256':body_hash,'reference_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()},indent=2)+'\n')
 print('Extracted original Move, SHA256',body_hash)

if __name__=='__main__':main()
