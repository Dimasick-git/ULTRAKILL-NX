"""Regression for the installed r33 Bootstrap black screen."""
from pathlib import Path
import tempfile
from stage_full_game import BOOT_FILES,RUNTIME_FILES,overlay_runtime,sha

def main():
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);base=root/'game';export=root/'editor';out=root/'out'
        for folder,prefix in ((base,b'game graph '),(export,b'editor scaffold ')):
            folder.mkdir()
            for name in BOOT_FILES+RUNTIME_FILES+('hashed-dependency','Resources/unity_builtin_extra'):
                target=folder/name;target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(prefix+name.encode())
        original={name:sha(base/name) for name in BOOT_FILES+RUNTIME_FILES}
        manifest=overlay_runtime(base,export,out)
        for name in BOOT_FILES+('hashed-dependency','Resources/unity_builtin_extra'):
            assert (out/name).read_bytes()==(base/name).read_bytes(),name
        for name in RUNTIME_FILES:
            assert (out/name).read_bytes()==(export/name).read_bytes(),name
        assert all(sha(base/name)==digest for name,digest in original.items())
        assert all(sha(out/name)==digest for name,digest in manifest.items())
        try:overlay_runtime(export,export,root/'invalid')
        except AssertionError:pass
        else:raise AssertionError('Accepted editor Bootstrap as game baseline')
        assert not (root/'invalid').exists()
    print('PASS: original scene/resources/dependencies retained, fresh code metadata paired, baseline unchanged, scaffold rejected')
if __name__=='__main__':main()
