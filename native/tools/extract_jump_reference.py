"""Adapt the archived Jump body; retain branches and arithmetic verbatim.

StopSlide/TrySSJ are recorded engine boundaries in this comparison, not
independently verified by this oracle. The generated game code stays local.
"""
import argparse, hashlib, json, re
from pathlib import Path

BASELINES = {'Jump':'58e095836726bafb3641513c4bec83a893c5a5381d5b72a8d9a32a7a891095a0','TryDash':'d16c6d5dadd688abd1ad71e62d61387e56ac5339a4d0cc32d8dd999a770d285f','Dodge':'2750707b230f87667c59c1af4896543eac4c7db6f333a114500e1e464dc9940a'}
BASELINES['StopSlide']='1262e88deac1b62fd56204d010de49179ecdf8e938cc9a915056dc9f015f1b9e'

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--method',choices=BASELINES,default='Jump');a=p.parse_args()
    raw=a.source.read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n')
    m=re.search(r'^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR void NewMovement_'+a.method+r'_m[0-9A-F]+[^\n]*\n\{',text,re.M)
    if not m:raise ValueError('Jump definition missing')
    start=m.end()-1;depth=0
    for end in range(start,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:break
    body=text[start:end+1];digest=hashlib.sha256(body.encode()).hexdigest()
    if digest!=BASELINES[a.method]:raise ValueError(a.method+' baseline changed: '+digest)
    body,n=re.subn(r'\n\s*static bool s_Il2CppMethodInitialized;\s*if \(!s_Il2CppMethodInitialized\)\s*\{.*?s_Il2CppMethodInitialized = true;\s*\}','',body,flags=re.S)
    assert n==1
    body=re.sub(r'^\s*il2cpp_codegen_runtime_class_init_inline\([^;]+;','',body,flags=re.M)
    body=re.sub(r'\bNullCheck\([^;]+;','',body)
    for t in ('Vector3','NewMovement','PlayerAnimations','Func_2','CustomGroundProperties','AudioSource','AudioClip','GroundCheckGroup','GameObject','Rigidbody','Transform','AssistController','TimeSince','U3CU3Ec','Quaternion','CameraController','RumbleManager','RumbleKey','PendingVibration','Object','Vector2','InputManager','PlayerInput','InputActionState','HookArm','WallCheckGroup'):
        body=re.sub(r'\b'+t+r'_t[0-9A-F]+\b',t,body)
    body=re.sub(r'\bU3CU3Ec_t[0-9A-F]+_StaticFields\b','DelegateCache',body)
    body=re.sub(r'\bRumbleProperties_t[0-9A-F]+_StaticFields\b','RumbleCache',body)
    body=re.sub(r'il2cpp_codegen_static_fields_for\(RumbleProperties[^)]*\)','rumbleCache()',body)
    body=re.sub(r'il2cpp_codegen_static_fields_for\([^)]*\)','delegateCache()',body)
    body=re.sub(r'il2cpp_codegen_object_new\([^)]*\)','newDelegate()',body)
    body=re.sub(r'\b[A-Za-z0-9_]+_RuntimeMethod_var\b','nullptr',body)
    body=body.replace('Il2CppCodeGenWriteBarrier','writeBarrier')
    body=body.replace('Object_Instantiate_TisGameObject_t76FEDD663AB33C991A9C9A23129337651094216F_m10D87C6E0708CA912BBB02555BF7D0FBC5D7A2B3','instantiate')
    body=body.replace('Object_Instantiate_TisGameObject_t76FEDD663AB33C991A9C9A23129337651094216F_m831D2F71DF2AA6C93AFDFEFA04CF2CFC5FBBCDB4','instantiateAt')
    body=body.replace('GameObject_GetComponent_TisAudioSource_t871AC2272F896738252F04EE949AEF5B241D3299_m4F30DAB8E5B57E8DB6BD8C0C3BB11CCB57690C4A','audioComponent')
    body=body.replace('MonoSingleton_1_get_Instance_m19E86BFA364A2E24BFCE12263216F6A5C3184E65','animations')
    body=body.replace('MonoSingleton_1_get_Instance_m8E1D73F37C187621AB995022ED8AD0D170259505','rumbleManager')
    body=body.replace('MonoSingleton_1_get_Instance_m01F6C138CCEAE348703130A9208D2AD2BF39F19B','inputManager')
    body=body.replace('MonoSingleton_1_get_Instance_mEFA94223D21E59F58D1F06714D0E25F282D8138B','hookArm')
    body=body.replace('InputActionState_ReadValue_TisVector2_t1FD6F485C871E832B347AB2DC8CBA08B739D8DF7_m9C5C9EEA633F571C855F47D34DE96B9158CD20F2','readMove')
    body=body.replace('Rigidbody_AddForce_m7A3EEEED21F986917107CBA6CC0106DCBC212198','Rigidbody_AddForce')
    body=re.sub(r'_m[0-9A-F]{40}(?:_inline)?\b','',body)
    body=body.replace('->___','->').replace('.___','.')
    for old,new in [('il2cpp_codegen_multiply','mul'),('il2cpp_codegen_add','add'),('il2cpp_codegen_subtract','sub')]:body=body.replace(old,new)
    body=body.replace('_stringLiteral844860FC7C7150E713F77FD943C6EBE876B5D651','NotJumping').replace('_stringLiteralD9CAB248D9B8943CEEBD293DFEEA3172399B4C96','JumpReady')
    if re.search(r'il2cpp|_t[0-9A-F]{40}|_m[0-9A-F]{40}|_stringLiteral',body):raise ValueError('unhandled generated dependency')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text('// Generated local game reference; do not publish.\nnamespace jump_reference {\ninline void original'+a.method+'(NewMovement* __this)\n'+body+'\n}\n')
    a.output.with_suffix('.json').write_text(json.dumps({'source':str(a.source),'source_sha256':hashlib.sha256(raw).hexdigest(),'original_method_sha256':digest,'scope':a.method+' body; StopSlide and TrySSJ are recorded boundaries, not validated implementations'},indent=2)+'\n')
    print('Extracted '+a.method+':',digest)

if __name__=='__main__':main()
