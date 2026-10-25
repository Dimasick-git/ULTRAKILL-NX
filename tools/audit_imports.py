"""Audit actual relocated import names against compiled loader tables/modules."""
from pathlib import Path
import argparse,json,struct
from derive_offsets import Elf

def dynamic_symbols(elf):
    table=elf.section('.dynsym');strings=elf.section('.dynstr');result=[]
    if not table:return result
    for offset in range(table['offset'],table['offset']+table['size'],24):
        name,info,other,section,value,size=struct.unpack_from('<IBBHQQ',elf.data,offset)
        if name:result.append((elf._cstr(strings['offset']+name),section,info>>4))
    return result

def audit(loader,paths):
    native=Elf(loader);symbols=native.symbols();available=set()
    for name in ('dynlib_functions','unity_dynlib_functions','firebase_extra_functions','nx_android_compat_functions'):
        va,size=symbols[name];assert size%16==0
        for offset in range(0,size,16):
            string,target=struct.unpack('<QQ',native.read_va(va+offset,16));assert target,name
            raw=bytearray()
            while len(raw)<512:
                value=native.read_va(string+len(raw),1)
                if value==b'\0':break
                raw+=value
            available.add(raw.decode())
    modules=[(Path(p),dynamic_symbols(Elf(p))) for p in paths]
    for _,entries in modules:available.update(name for name,section,_ in entries if section)
    report={}
    for p,entries in modules:
        imports={name for name,section,_ in entries if not section}
        missing=sorted(imports-available)
        report[p.name]={'undefined_names':len(imports),'resolved':len(imports)-len(missing),'unresolved':missing}
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--loader',required=True,type=Path);ap.add_argument('--library',required=True,action='append',type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args()
    report=audit(a.loader,a.library)
    result=json.dumps(report,indent=2);print(result)
    if a.report:a.report.write_text(result+'\n',encoding='utf-8')
    assert all(not r['unresolved'] for r in report.values()),'Unresolved native symbols remain'
