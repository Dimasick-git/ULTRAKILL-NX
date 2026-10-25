from pathlib import Path
import argparse,re,subprocess,tempfile
from patch_stain_transfer import patch,MARKER

def main():
    parser=argparse.ArgumentParser();parser.add_argument('file',type=Path);parser.add_argument('--cxx',default='g++');a=parser.parse_args()
    source=a.file.read_text(encoding='utf-8-sig');assert MARKER not in source
    result=patch(source);assert patch(result)==result
    match=re.search(re.escape(MARKER)+r'\n\t\t(if \(L_10\) (BloodstainParent_RemoveStain_m\w+)\(L_10, L_11, NULL\);)',result)
    assert match
    old='\t\tNullCheck(L_10);\n\t\t'+match[1][len('if (L_10) '):]
    assert result.replace('\t\t'+MARKER+'\n\t\t'+match[1],old)==source
    # Compile the exact changed production statement, including continuation.
    program='''#include <cassert>
#include <cstddef>
struct Parent { int detached=0; };
void DETACH(Parent* p,int i,void*) { assert(p);p->detached=i; }
int transfer(Parent* L_10,int L_11){ STATEMENT return 123; }
int main(){Parent parent;assert(transfer(&parent,7)==123 && parent.detached==7);assert(transfer(nullptr,7)==123);}
'''.replace('DETACH',match[2]).replace('STATEMENT',match[1])
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'transfer.cpp';exe=Path(tmp)/'transfer.exe';src.write_text(program)
        subprocess.run([a.cxx,'-std=c++17','-O2',str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
    print('PASS: exact production detach statement; valid parent unchanged; absent old parent continues reassignment; idempotent; all other code identical')

if __name__=='__main__':main()
