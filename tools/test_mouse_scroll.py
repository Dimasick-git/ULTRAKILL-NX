"""Compile the actual exported scroll branch with/without a desktop mouse."""
from pathlib import Path
import argparse, re, subprocess, tempfile
from patch_mouse_scroll import patch, METHOD

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('generated',type=Path)
    ap.add_argument('--cc',default='g++')
    args=ap.parse_args()
    text=args.generated.read_text(encoding='utf-8')
    assert patch(text)==text, 'Build lacks the mouse guard'
    original=text.replace('\t\t// NX: absent desktop mouse means zero scroll; continue gamepad logic.\n\t\tif (!L_195) { V_2 = 0.0f; goto IL_060a; }\n','')
    assert patch(original)==text, 'Patch changed other code'
    method=text[text.index('void '+METHOD+' ('):]
    start=method.index('\t\tMouse_t')
    end=method.index('IL_060a:')+len('IL_060a:')
    code=method[start:end]
    code += ' ;\n'
    code=re.sub(r'Mouse_t\w+', 'Mouse', code)
    code=re.sub(r'DeltaControl_t\w+', 'DeltaControl', code)
    code=re.sub(r'Vector2_t\w+', 'Vector2', code)
    code=re.sub(r'InputManager_t\w+', 'InputManager', code)
    code=re.sub(r'Mouse_get_current_m\w+', 'get_mouse', code)
    code=re.sub(r'Mouse_get_scroll_m\w+', 'get_scroll', code)
    code=re.sub(r'InputControl_1_ReadValue_m\w+_RuntimeMethod_var', 'nullptr', code)
    code=re.sub(r'InputControl_1_ReadValue_m\w+', 'read_scroll', code)
    code=code.replace('il2cpp_codegen_multiply','multiply')
    # The first brace closes the original scroll scope before ScrRev inversion.
    source=r'''
#include <cassert>
#include <stdexcept>
struct Vector2 {float ___y;};
struct DeltaControl {float y;};
struct Mouse {DeltaControl scroll;};
struct InputManager {bool ___ScrRev;};
struct Gun {InputManager* ___inman;};
static Mouse* current;
static Mouse* get_mouse(void*) {return current;}
static DeltaControl* get_scroll(Mouse* p,void*) {return &p->scroll;}
static Vector2 read_scroll(DeltaControl* p,void*) {return {p->y};}
static float multiply(float a,float b) {return a*b;}
template<class T> void NullCheck(T* p) {if(!p) throw std::runtime_error("null");}
static float run(Gun* __this,bool& continued) { float V_2=-99; {
'''+code+r'''
continued=true; return V_2; }
int main() {
 InputManager input{false}; Gun gun{&input}; Mouse mouse{{2.0f}};bool continued=false;
 current=nullptr; assert(run(&gun,continued)==0 && continued);
 continued=false; current=&mouse; assert(run(&gun,continued)==2 && continued);
 continued=false; input.___ScrRev=true; assert(run(&gun,continued)==-2 && continued);
 current=nullptr; continued=false; assert(run(&gun,continued)==0 && continued);
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src=Path(tmp)/'scroll.cpp';exe=Path(tmp)/'scroll.exe'
        src.write_text(source)
        subprocess.run([args.cc,'-std=c++17','-O2','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: absent mouse continues gamepad path; real mouse and reversed scroll preserved; patch isolated/idempotent')

if __name__=='__main__':main()
