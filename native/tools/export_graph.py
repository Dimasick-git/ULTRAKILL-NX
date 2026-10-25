"""Convert authored hierarchy/ownership without discarding inactive objects.

Component field payloads remain in the migration snapshot and typed system
packs. This graph preserves ownership and activation, not script behavior.
"""
from pathlib import Path
import argparse, collections, gzip, hashlib, json, math, struct, zlib

def local_ref(value):
    r=value['$ref']
    if r['file']:raise ValueError('external hierarchy/component ownership reference')
    return r['path']

def convert(world):
    if world['format']!='NXWORLD-MIGRATION-1':raise ValueError('unknown world snapshot')
    objects={o['id']:o for o in world['objects']}
    if len(objects)!=len(world['objects']):raise ValueError('duplicate source object ID')
    gos={key:o for key,o in objects.items() if o['type']=='GameObject'}
    if not 0<len(gos)<=65536:raise ValueError('hierarchy node budget exceeded')
    transforms={key:o for key,o in objects.items() if o['type'] in ('Transform','RectTransform')}
    by_go={}
    for key,o in transforms.items():
        owner=local_ref(o['data']['m_GameObject'])
        if owner not in gos or owner in by_go:raise ValueError('invalid transform owner')
        by_go[owner]=key
    if set(by_go)!=set(gos):raise ValueError('GameObject without transform')
    keys=sorted(gos,key=int);indices={key:i for i,key in enumerate(keys)}
    names=bytearray();interned={};nodes=bytearray();components=bytearray();component_count=0;used=set();parents=[];active_self=[];counts=collections.Counter()
    def string(value):
        if value not in interned:
            encoded=value.encode('utf-8');interned[value]=(len(names),len(encoded));names.extend(encoded)
        return interned[value]
    for key in keys:
        go=gos[key]['data'];tid=by_go[key];t=transforms[tid]['data'];parent=local_ref(t['m_Father'])
        if not 0<=go['m_Layer']<32 or not 0<=go['m_Tag']<=65535:raise ValueError('invalid layer/tag')
        if parent!='0' and parent not in transforms:raise ValueError('missing parent transform')
        parent_index=-1 if parent=='0' else indices[local_ref(transforms[parent]['data']['m_GameObject'])]
        parents.append(parent_index);active_self.append(bool(go['m_IsActive']))
        first=component_count
        for entry in go['m_Component']:
            pointer=entry.get('component',entry.get('second'))
            cid=local_ref(pointer)
            if cid not in objects or cid in used:raise ValueError('missing or multiply owned component '+cid)
            component=objects[cid];data=component['data']
            if local_ref(data['m_GameObject'])!=key:raise ValueError('component owner mismatch')
            used.add(cid);kind=component['type'];script=component.get('script');script_name=(script['assembly']+'::'+'.'.join(v for v in (script['namespace'],script['class']) if v)) if script else ''
            flags=(1 if data.get('m_Enabled',1) else 0)|(2 if 'm_Enabled' in data else 0)
            components.extend(struct.pack('<q6I',int(cid),indices[key],*string(kind),*string(script_name),flags));component_count+=1;counts[kind]+=1
        if tid not in used:raise ValueError('transform not in GameObject component list')
        p=[t['m_LocalPosition'][c] for c in 'xyz'];q=[t['m_LocalRotation'][c] for c in 'xyzw'];s=[t['m_LocalScale'][c] for c in 'xyz']
        if not all(math.isfinite(v) for v in p+q+s) or abs(sum(v*v for v in q)-1)>.002:raise ValueError('invalid local TRS')
        flags=(1 if go['m_IsActive'] else 0)|(2 if transforms[tid]['type']=='RectTransform' else 0)
        nodes.extend(struct.pack('<qqi7I10f',int(key),int(tid),parent_index,first,component_count-first,*string(go['m_Name']),flags,go['m_Layer'],go['m_Tag'],*p,*q,*s))
    active_cache={};max_depth=0
    for index in range(len(keys)):
        path=[];seen=set();at=index
        while at!=-1:
            if at in seen:raise ValueError('cyclic hierarchy')
            seen.add(at);path.append(at);at=parents[at]
            if len(path)>512:raise ValueError('hierarchy depth budget exceeded')
        max_depth=max(max_depth,len(path))
        active=True
        for at in reversed(path):active=active and active_self[at]
        active_cache[index]=active
    if component_count>524288 or len(names)>16777216:raise ValueError('hierarchy component/string budget exceeded')
    payload=nodes+components+names
    header=struct.pack('<8s6I',b'NXGRAPH\0',1,len(keys),component_count,len(names),zlib.crc32(payload),0)
    report={'format':'NXGRAPH1','nodes':len(keys),'components':component_count,'component_types':dict(counts),'active_in_hierarchy':sum(active_cache.values()),'inactive_in_hierarchy':len(keys)-sum(active_cache.values()),'maximum_depth':max_depth,'strings_bytes':len(names),'unbound_records_retained_in_world_snapshot':len(objects)-len(gos)-len(used),'script_behavior_implemented':False,'ui_layout_implemented':False}
    return header+payload,report

def main():
    p=argparse.ArgumentParser();p.add_argument('--world',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    raw=a.world.read_bytes();world=json.loads(gzip.decompress(raw));pack,report=convert(world)
    a.output.parent.mkdir(parents=True,exist_ok=True);temp=a.output.with_name(a.output.name+'.tmp');temp.write_bytes(pack);temp.replace(a.output)
    report.update(world_sha256=hashlib.sha256(raw).hexdigest(),source_bundle_sha256=world['bundle_sha256'],pack_sha256=hashlib.sha256(pack).hexdigest(),bytes=len(pack))
    a.output.with_suffix('.graph.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
