"""Test the actual full-release overlay selection without running publication."""
import ast
from pathlib import Path

path=Path(__file__).with_name('repackage_release.py')
tree=ast.parse(path.read_text(encoding='utf-8'))
nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('safe_name','keep_patch_member')]
assert len(nodes)==2
scope={}
exec(compile(ast.Module(body=nodes,type_ignores=[]),str(path),'exec'),scope)
keep=scope['keep_patch_member']
for p in ('switch/ultrakill/libil2cpp.so','switch/ultrakill/assets/bin/Data/Managed/Metadata/global-metadata.dat',
          'atmosphere/contents/0000000000534C56/exefs.nsp','SaltySD/saltynx_core.elf',
          'atmosphere/exefs_patches/SaltyNX_Fixes/a.ips','licenses/SaltyNX-UPSTREAM-README.md'):
    assert keep(p),p
for p in ('switch/ultrakill/il2cpp/Metadata/global-metadata.dat','README_RU.txt','SHA256SUMS.txt',
          'switch/prod.keys','atmosphere/contents/OTHER/exefs.nsp','SaltySD/plugins/FSR/user.ini'):
    assert not keep(p),p
for p in ('../escape','/absolute','switch\\escape'):
    try:keep(p)
    except ValueError:pass
    else:raise AssertionError(p)
print('PASS: r32 game/module overlay, fresh metadata cache, unrelated paths and traversal excluded')
