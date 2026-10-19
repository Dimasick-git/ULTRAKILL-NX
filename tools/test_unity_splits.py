"""Verify read-only RomFS staging joins complete Unity files and rejects gaps."""
from pathlib import Path
import tempfile
from stage_full_game import join_unity_splits

def payload():
    data=bytearray(range(128));data[8:12]=(22).to_bytes(4,'big');data[24:32]=len(data).to_bytes(8,'big');return bytes(data)
def main():
    data=payload()
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp)
        (root/'scene.assets.split0').write_bytes(data[:63]);(root/'scene.assets.split1').write_bytes(data[63:])
        join_unity_splits(root)
        assert (root/'scene.assets').read_bytes()==data and not list(root.glob('*.split*'))
        join_unity_splits(root);assert (root/'scene.assets').read_bytes()==data
        (root/'missing.assets.split0').write_bytes(data[:63]);(root/'missing.assets.split2').write_bytes(data[63:])
        try:join_unity_splits(root)
        except AssertionError:pass
        else:raise AssertionError('Accepted a missing middle part')
        assert not (root/'missing.assets').exists()
        (root/'missing.assets.split2').unlink()
        try:join_unity_splits(root)
        except AssertionError:pass
        else:raise AssertionError('Accepted a truncated serialized file')
    print('PASS: exact joined bytes, size header, split removal, idempotence, missing and truncated parts rejected')
if __name__=='__main__':main()
