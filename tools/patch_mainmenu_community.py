"""Replace the shipped main-menu social row and its local catalog cache key.

The defaults in the bundle must be correct before any runtime callback runs.
Unchanged serialized objects are checked byte-for-byte after repacking.
"""
from pathlib import Path
import argparse, base64, hashlib, json, struct, sys

CHANNEL = 'https://t.me/RYAZHENKACFW'
CAPTION = '<mark=#000000>Telegram: @RYAZHENKACFW</mark>'
DISCORD = 'https://discord.com/invite/newblood'
REMOVED = {'https://youtube.com/c/Hakita', 'https://twitter.com/ULTRAKILLGame'}

def digest(data): return hashlib.sha256(data).hexdigest()
def atomic(path, data):
    temporary = path.with_name(path.name + '.nx-tmp')
    temporary.write_bytes(data); temporary.replace(path)

def patch(game, report_path=None):
    import UnityPy
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'native/tools'))
    from addressable_catalog import Catalog
    catalog_path = game / 'assets/aa/catalog.json'
    catalog = Catalog(catalog_path)
    hits = [i for i in range(len(catalog.entries)) if catalog.internal(i).endswith('/specialscenes_scenes_mainmenu.bundle')]
    assert len(hits) == 1, 'Main-menu bundle location is ambiguous'
    entry_index = hits[0]; bundle_path = catalog.bundle_path(entry_index)
    before = bundle_path.read_bytes(); env = UnityPy.load(before)
    all_objects = {(o.assets_file.name, o.path_id): o for o in env.objects}
    original = {key: digest(o.get_raw_data()) for key, o in all_objects.items()}
    slots = []
    for obj in all_objects.values():
        if obj.type.name == 'MonoBehaviour':
            data = obj.read_typetree()
            if data.get('url') in (DISCORD, CHANNEL): slots.append(obj)
    assert len(slots) == 1, 'Expected exactly one community slot'
    target_file = slots[0].assets_file.name
    objects = {pid:o for (name,pid),o in all_objects.items() if name == target_file}
    changes = {}; links = []
    for obj in objects.values():
        if obj.type.name != 'MonoBehaviour': continue
        data = obj.read_typetree()
        if 'url' in data: links.append((obj.path_id, data))
    matches = [(pid,d) for pid,d in links if d['url'] in (DISCORD, CHANNEL)]
    assert len(matches) == 1, 'Expected exactly one community slot'
    url_id, url = matches[0]; go_id = url['m_GameObject']['m_PathID']
    def read(pid): return changes.get(pid, objects[pid].read_typetree())
    def rect_for(go):
        ids = [x['component']['m_PathID'] for x in go['m_Component']]
        return next(pid for pid in ids if objects[pid].type.name == 'RectTransform')
    go = read(go_id); root_id = rect_for(go); root = read(root_id)
    removed = []
    for pid, data in links:
        if data['url'] not in REMOVED: continue
        sibling_go_id = data['m_GameObject']['m_PathID']; sibling_go = read(sibling_go_id)
        sibling_rect = read(rect_for(sibling_go))
        if sibling_rect['m_Father'] == root['m_Father']:
            removed.append((sibling_go_id, sibling_go, sibling_rect))
    assert len(removed) == 2, 'Expected YouTube and Twitter in the same native row'
    rects = [root] + [r for _,_,r in removed]
    for r in rects:
        assert r['m_AnchorMin'] == root['m_AnchorMin'] and r['m_AnchorMax'] == root['m_AnchorMax']
        assert r['m_LocalScale'] == root['m_LocalScale'] and r['m_LocalRotation'] == root['m_LocalRotation']
    low = {axis:min(r['m_AnchoredPosition'][axis] - r['m_SizeDelta'][axis] * r['m_Pivot'][axis] for r in rects) for axis in ('x','y')}
    high = {axis:max(r['m_AnchoredPosition'][axis] + r['m_SizeDelta'][axis] * (1-r['m_Pivot'][axis]) for r in rects) for axis in ('x','y')}
    root['m_Pivot'] = {'x':0.,'y':1.}
    root['m_AnchoredPosition'] = {'x':low['x'],'y':high['y']}
    root['m_SizeDelta'] = {axis:high[axis]-low[axis] for axis in ('x','y')}
    assert root['m_SizeDelta']['x'] >= 338, 'Not enough room for complete channel caption'
    changes[root_id] = root; go['m_Name'] = 'NX Community'; changes[go_id] = go
    url['url'] = CHANNEL; changes[url_id] = url
    for pid, data, _ in removed: data['m_IsActive'] = False; changes[pid] = data
    # Find the native caption, preserving its font, material and hover callbacks.
    captions = []
    def visit(rect_id):
        tr = read(rect_id); node = read(tr['m_GameObject']['m_PathID'])
        for component in node['m_Component']:
            pid = component['component']['m_PathID']
            if objects[pid].type.name == 'MonoBehaviour':
                data = read(pid)
                if 'm_text' in data and 'm_fontAsset' in data: captions.append((pid,data,rect_id))
        for child in tr['m_Children']: visit(child['m_PathID'])
    visit(root_id); assert len(captions) == 2, 'Expected native normal and highlighted captions'
    for caption_id, caption, caption_rect_id in captions:
        caption.update(m_text=CAPTION if '<mark=' in caption['m_text'] else 'Telegram: @RYAZHENKACFW', m_enableAutoSizing=True, m_fontSizeMin=18., m_fontSizeMax=24.,
                   m_enableWordWrapping=False, m_overflowMode=0, m_HorizontalAlignment=2, m_VerticalAlignment=512, m_RaycastTarget=False)
        changes[caption_id] = caption
        caption_rect = read(caption_rect_id)
        caption_rect.update(m_AnchorMin={'x':0.,'y':0.}, m_AnchorMax={'x':1.,'y':1.},
                        m_AnchoredPosition={'x':0.,'y':0.}, m_SizeDelta={'x':-12.,'y':0.})
        changes[caption_rect_id] = caption_rect
    for pid,data in changes.items(): objects[pid].save_typetree(data)
    result = env.file.save(packer='lz4')
    check = UnityPy.load(result)
    all_reopened = {(o.assets_file.name,o.path_id):o for o in check.objects}
    assert set(all_reopened) == set(all_objects), 'Serialized object graph changed'
    for key, expected in original.items():
        if key[0] != target_file or key[1] not in changes:
            assert digest(all_reopened[key].get_raw_data()) == expected, f'Unrelated object changed: {key}'
    reopened = {pid:o for (name,pid),o in all_reopened.items() if name == target_file}
    assert all(reopened[pid].read_typetree()['m_text'] == data['m_text'] for pid,data,_ in captions)
    assert reopened[url_id].read_typetree()['url'] == CHANNEL
    assert all(not reopened[pid].read_typetree()['m_IsActive'] for pid,_,_ in removed)
    atomic(bundle_path, result)
    # Append one replacement request object; all existing extra-data offsets stay
    # valid. Local bundles deliberately have CRC disabled in this GLES port.
    data = catalog.data; extra = base64.b64decode(data['m_ExtraDataString'])
    offset = catalog.entries[entry_index][4]; assert extra[offset] == 7
    pos = offset + 1; pos += 1 + extra[pos]; pos += 1 + extra[pos]
    length = struct.unpack_from('<i', extra, pos)[0]; start = pos + 4
    options = json.loads(extra[start:start+length].decode('utf-16-le'))
    assert options['m_Crc'] == 0 and not options['m_UseUWRForLocalBundles'], 'Unexpected local bundle validation policy'
    options['m_Hash'] = hashlib.md5(result).hexdigest(); options['m_BundleSize'] = len(result)
    encoded = json.dumps(options, separators=(',',':'),ensure_ascii=False).encode('utf-16-le')
    replacement = extra[offset:pos] + struct.pack('<i',len(encoded)) + encoded
    entries = bytearray(base64.b64decode(data['m_EntryDataString']))
    struct.pack_into('<i',entries,4+entry_index*28+16,len(extra))
    data['m_EntryDataString'] = base64.b64encode(entries).decode()
    data['m_ExtraDataString'] = base64.b64encode(extra+replacement).decode()
    data['m_BuildResultHash'] = hashlib.md5((data['m_BuildResultHash']+options['m_Hash']).encode()).hexdigest()
    catalog_bytes = json.dumps(data,separators=(',',':'),ensure_ascii=False).encode()
    atomic(catalog_path,catalog_bytes)
    if catalog_path.with_suffix('.hash').exists(): atomic(catalog_path.with_suffix('.hash'), hashlib.md5(catalog_bytes).hexdigest().encode())
    reopened_catalog = Catalog(catalog_path)
    assert reopened_catalog.keys == catalog.keys and reopened_catalog.buckets == catalog.buckets
    report = {'bundle':str(bundle_path),'input_sha256':digest(before),'sha256':digest(result),
              'caption':CAPTION,'url':CHANNEL,'width':root['m_SizeDelta']['x'],'changed_objects':sorted(changes),
              'unchanged_objects_verified':len(original)-len(changes),'catalog_cache_key':options['m_Hash']}
    if report_path: atomic(report_path,(json.dumps(report,indent=2)+'\n').encode())
    return report

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--game',required=True,type=Path)
    ap.add_argument('--pylib',type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args()
    if a.pylib: sys.path.insert(0,str(a.pylib))
    print(json.dumps(patch(a.game,a.report),indent=2))
