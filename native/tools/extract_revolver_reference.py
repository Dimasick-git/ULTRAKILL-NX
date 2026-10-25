"""Keep the archived Revolver.Update control flow verbatim for host checks.

Generated game code stays outside the repository. Shoot/ThrowCoin and engine
effects are recorded boundaries, not implementations validated by this oracle.
"""
from pathlib import Path
import argparse,hashlib,json,re
BASELINE='89a9e214d94bf453087ce602abfa539841b96175d82810b69100cb618387dd86'
def main():
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args();raw=a.source.read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n')
 m=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void Revolver_Update_m[0-9A-F]+[^\n]*\n\{',text,re.M)
 if not m:raise ValueError('archived Revolver.Update missing')
 start=m.end()-1;depth=0
 for end in range(start,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if not depth:break
 body=text[start:end+1];digest=hashlib.sha256(body.encode()).hexdigest()
 if digest!=BASELINE:raise ValueError('Revolver.Update baseline changed: '+digest)
 body,count=re.subn(r'\n\s*static bool s_Il2CppMethodInitialized;\s*if \(!s_Il2CppMethodInitialized\)\s*\{.*?s_Il2CppMethodInitialized = true;\s*\}','',body,flags=re.S);assert count==1
 body=re.sub(r'^\s*il2cpp_codegen_runtime_class_init_inline\([^;]+;','',body,flags=re.M)
 body=body.replace('MonoSingleton_1_get_Instance_m563F215B0D2AFDF5D5A791509A81483C964CF12F','colorSettings').replace('MonoSingleton_1_get_Instance_m8E1D73F37C187621AB995022ED8AD0D170259505','rumbleManager')
 body=body.replace('colorSettings_RuntimeMethod_var','nullptr').replace('rumbleManager_RuntimeMethod_var','nullptr')
 body=re.sub(r'_t[0-9A-F]{40}(?=\b|_)','',body);body=re.sub(r'_m[0-9A-F]{40}(?:_inline)?\b','',body);body=body.replace('->___','->').replace('.___','.')
 for old,new in [('il2cpp_codegen_add','add'),('il2cpp_codegen_subtract','sub'),('il2cpp_codegen_multiply','mul')]:body=body.replace(old,new)
 for token,name in {'D16DBF53C22BCE367FC4ED51E06A78B001D94B9F':'DelayedSuper','862C1DED77D2A5F49F21A39C168B672A483BD1F0':'DelayedNormal','210A57E48D75693826CED344F74424E34D3E9265':'DelayedCoin','4B8146FB95E4F51B29DA41EB5F6D60F8FD0ECF21':'MainTexture','47A3FAF17D89549FD0F0ECA7370B81F7C80DFCDE':'TintColor'}.items():body=body.replace('_stringLiteral'+token,name)
 if re.search(r'_t[0-9A-F]{40}|_m[0-9A-F]{40}|_stringLiteral',body):raise ValueError('unhandled generated token')
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text('// Local archived game reference; do not publish.\nnamespace revolver_reference {\ninline void originalUpdate(Revolver* __this)\n'+body+'\n}\n')
 a.output.with_suffix('.json').write_text(json.dumps({'source':str(a.source),'source_sha256':hashlib.sha256(raw).hexdigest(),'method_sha256':digest,'scope':'unmodified archived Update branches/arithmetic; Shoot, ThrowCoin, CheckCoinCharges and engine effects are recorded boundaries; LateUpdate and full combat not validated'},indent=2)+'\n')
if __name__=='__main__':main()
