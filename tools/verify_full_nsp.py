"""Read an NSP back, verify NCA/IVFC hashes and every embedded payload file.

Requires cryptography. Keys are read locally, never emitted or copied.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes

TITLE=0x01005CE3B1A40000
def sha(data): return hashlib.sha256(data).digest()
def npdm_policy(data):
    data=bytearray(data);assert data[:4]==b'META'
    aci,_,acid,_=struct.unpack_from('<4I',data,0x70)
    assert data[aci:aci+4]==b'ACI0' and struct.unpack_from('<Q',data,aci+0x10)[0]==TITLE
    assert data[acid+0x200:acid+0x204]==b'ACID'
    # hacBrewPack replaces only this public signing modulus in the source NPDM.
    data[acid+0x100:acid+0x200]=bytes(0x100)
    return sha(data).hex()
def pfs(read, size):
    magic,count,strings,_=struct.unpack('<4sIII',read(0,16));assert magic==b'PFS0' and 0<count<1000
    start=16+24*count+strings; names=read(16+24*count,strings); entries={}
    for i in range(count):
        offset,length,name,_=struct.unpack('<QQII',read(16+i*24,24))
        name=names[name:].split(b'\0',1)[0].decode();assert '/' not in name and name not in entries
        assert start+offset+length<=size
        entries[name]=(start+offset,length)
    return entries

class Nca:
    def __init__(self,file,base,size,keys):
        self.file,self.base,self.size=file,base,size
        raw=self.raw(0,0xc00);key=bytes.fromhex(keys['header_key'])
        self.header=b''.join(Cipher(algorithms.AES(key),modes.XTS(i.to_bytes(16,'big'))).decryptor().update(raw[i*512:(i+1)*512]) for i in range(6))
        h=self.header;assert h[0x200:0x204]==b'NCA3' and struct.unpack_from('<Q',h,0x208)[0]==size
        assert struct.unpack_from('<Q',h,0x210)[0]==TITLE
        self.sections=[]
        for i in range(4):
            start,end=struct.unpack_from('<II',h,0x240+i*16)
            if not end:continue
            fh=h[0x400+i*512:0x600+i*512];assert sha(fh)==h[0x280+i*32:0x2a0+i*32]
            assert start<end and end*512<=size
            self.sections.append((start*512,end*512,fh))
        generation=max(h[0x206],h[0x220]);slot=max(generation-1,0)
        kaek=bytes.fromhex(keys[f'key_area_key_application_{slot:02x}'])
        area=Cipher(algorithms.AES(kaek),modes.ECB()).decryptor().update(h[0x300:0x340]);self.key=area[32:48]
    def raw(self,offset,size):
        assert offset>=0 and offset+size<=self.size
        self.file.seek(self.base+offset);data=self.file.read(size);assert len(data)==size;return data
    def read(self,section,offset,size):
        start,end,fh=section;assert offset>=0 and start+offset+size<=end
        if fh[4]==1:return self.raw(start+offset,size)
        assert fh[4]==3
        at=start+offset;aligned=at&~15;skip=at-aligned
        iv=fh[0x140:0x148][::-1]+(aligned//16).to_bytes(8,'big')
        dec=Cipher(algorithms.AES(self.key),modes.CTR(iv)).decryptor()
        return dec.update(self.raw(aligned,size+skip))[skip:]
    def romfs(self,section):
        fh=section[2];assert fh[3]==3 and fh[8:12]==b'IVFC'
        levels=[struct.unpack_from('<QQII',fh,0x18+24*i)[:3] for i in range(6)]
        offset,length,_=levels[0]
        assert sha(self.read(section,offset,length))==fh[0xc8:0xe8], 'IVFC master hash'
        for level in range(1,6):
            po,ps,_=levels[level-1];parent=self.read(section,po,ps)
            co,cs,power=levels[level];block=1<<power;assert block==0x4000
            number=0
            for at in range(0,cs,4*1024*1024):
                data=self.read(section,co+at,min(cs-at,4*1024*1024))
                for pos in range(0,len(data),block):
                    piece=data[pos:pos+block];piece+=b'\0'*(block-len(piece))
                    assert sha(piece)==parent[number*32:(number+1)*32], f'IVFC level {level+1} block {number}'
                    number+=1
        return levels[5][0],levels[5][1]
    def pfs(self,section):
        fh=section[2];assert fh[3]==2
        block=struct.unpack_from('<I',fh,0x28)[0]
        ho,hs,po,ps=struct.unpack_from('<4Q',fh,0x30)
        table=self.read(section,ho,hs);assert sha(table)==fh[8:40], 'PFS master hash'
        for i,at in enumerate(range(0,ps,block)):
            assert sha(self.read(section,po+at,min(block,ps-at)))==table[i*32:(i+1)*32], 'PFS block hash'
        return po,ps,pfs(lambda offset,size:self.read(section,po+offset,size),ps)

def romfs_files(read):
    h=struct.unpack('<10Q',read(0,80));assert h[0]==80
    dirs=read(h[3],h[4]);files=read(h[7],h[8]);result={};seen=set()
    def walk(offset,path,depth=0):
        assert depth<64 and offset not in seen;seen.add(offset)
        _,sibling,child,file,_,length=struct.unpack_from('<6I',dirs,offset)
        name=dirs[offset+24:offset+24+length].decode('utf-8')
        assert '/' not in name and name not in ('.','..')
        here=path+'/'+name if path and name else name or path
        while file!=0xffffffff:
            _,nextfile,at,size,_,n=struct.unpack_from('<IIQQII',files,file)
            name=files[file+32:file+32+n].decode('utf-8');assert '/' not in name and name not in ('.','..')
            key=here+'/'+name if here else name;assert key not in result
            result[key]=(h[9]+at,size);file=nextfile
        while child!=0xffffffff:
            nextchild=struct.unpack_from('<I',dirs,child+4)[0]
            walk(child,here,depth+1);child=nextchild
    walk(0,'');return result

def verify(path,keys_path):
    keys={}
    for line in Path(keys_path).read_text().splitlines():
        if '=' in line:
            key,value=line.split('=',1);keys[key.strip()]=value.strip()
    with Path(path).open('rb') as f:
        def read(offset,size): f.seek(offset);return f.read(size)
        entries=pfs(read,Path(path).stat().st_size);program=None;exefs=None
        assert len(entries)==3 and all(n.endswith('.nca') for n in entries)
        for name,(base,length) in entries.items():
            digest=hashlib.sha256();f.seek(base);remaining=length
            while remaining:
                data=f.read(min(8*1024*1024,remaining));assert data;digest.update(data);remaining-=len(data)
            assert name.split('.')[0]==digest.hexdigest()[:32], 'NCA content ID mismatch'
            archive=Nca(f,base,length,keys)
            for section in archive.sections:
                if section[2][3]==2:
                    parsed=archive.pfs(section)
                    if archive.header[0x205]==0:exefs=(archive,section,parsed)
                elif archive.header[0x205]!=0:archive.romfs(section)
            if archive.header[0x205]==0:program=archive
        assert program
        section=next(s for s in program.sections if s[2][3]==3)
        romoff,romsize=program.romfs(section)
        def readrom(offset,size): assert offset+size<=romsize;return program.read(section,romoff+offset,size)
        files=romfs_files(readrom);at,size=files['install-manifest.json']
        manifest=json.loads(readrom(at,size));assert manifest['version']=='r35'
        archive,exesection,(exepos,_,exefiles)=exefs
        for name,expected in manifest['launcher'].items():
            offset,length=exefiles[name]
            assert sha(archive.read(exesection,exepos+offset,length)).hex()==expected, 'Launcher integrity'
        offset,length=exefiles['main.npdm']
        assert npdm_policy(archive.read(exesection,exepos+offset,length))==manifest['npdm_policy_sha256'], 'NPDM permissions/memory policy'
        assert set(files)==set(manifest['files'])|{'install-manifest.json'}
        for name,record in manifest['files'].items():
            at,size=files[name];assert size==record['bytes'],name
            digest=hashlib.sha256()
            for offset in range(0,size,4*1024*1024):digest.update(readrom(at+offset,min(4*1024*1024,size-offset)))
            assert digest.hexdigest()==record['sha256'],name
        required={'ultrakill/ultrakill_nx.nro','ultrakill/libil2cpp.so','ultrakill/libunity.so','ultrakill/offsets.ini','ultrakill/assets/bin/Data/Managed/Metadata/global-metadata.dat','ultrakill/assets/bin/Data/globalgamemanagers.assets','ultrakill/assets/bin/Data/sharedassets0.assets'}
        assert required<=set(files)
        at,size=files['ultrakill/assets/NxPort/boot-content.json']
        boot=json.loads(readrom(at,size));assert boot['source']=='ported-game'
        from stage_full_game import BOOT_FILES,RUNTIME_FILES
        assert set(boot['files'])==set(BOOT_FILES+RUNTIME_FILES)
        for name,expected in boot['files'].items():
            assert manifest['files']['ultrakill/assets/bin/Data/'+name]['sha256']==expected, 'Boot graph/metadata provenance mismatch'
    with Path(path).open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
    return {'file':str(Path(path).resolve()),'version':'r35','title_id':f'{TITLE:016X}', 'bytes':Path(path).stat().st_size,'sha256':digest,'payload_files':len(files),'nca_hashes':True,'romfs_ivfc':True,'payload_hashes':True}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('nsp',type=Path);ap.add_argument('--keys',required=True,type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args()
    report=verify(a.nsp,a.keys)
    if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
