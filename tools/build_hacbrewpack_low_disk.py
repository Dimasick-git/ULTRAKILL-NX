"""Rebuild the local packer; free completed RomFS intermediates before NSP copy.

Inputs and older installers are untouched. Only the packer's own --tempdir is
removed, after every NCA (including metadata) has been completed and closed.
"""
from pathlib import Path
import argparse, subprocess, re
from concurrent.futures import ThreadPoolExecutor

def main():
    p=argparse.ArgumentParser()
    for key in ('source','output','cc'):p.add_argument('--'+key,required=True,type=Path)
    a=p.parse_args();assert not a.output.exists();a.output.mkdir(parents=True)
    old='    // Create NSP\n'
    text=(a.source/'main.c').read_text()
    assert text.count(old)==1
    text=text.replace(old,'''    /* NX_RELEASE_TEMP_BEFORE_NSP: all NCA writers have returned. */
    if (filepath_remove_directory(&settings.temp_dir) != 0) {
        fprintf(stderr, "NX_RELEASE_TEMP_BEFORE_NSP: cannot remove work tempdir\\n");
        return EXIT_FAILURE;
    }
    os_makedir(settings.temp_dir.os_path);

'''+old)
    (a.output/'main.c').write_text(text)
    text=(a.source/'romfs.c').read_text()
    anchor='    while (cur_file != NULL)\n    {\n        FILE *f_in'
    replacement='    while (cur_file != NULL)\n    {\n        read_size = 0x61A8000; /* Reset per file. */\n        FILE *f_in'
    assert text.count(anchor)==1;text=text.replace(anchor,replacement)
    (a.output/'romfs.c').write_text(text)
    objects=[]
    for name in ('sha','aes','extkeys','pki','utils','main','filepath','ConvertUTF','nca','romfs','pfs0','ivfc','nacp','npdm','cnmt','rsa'):
        src=(a.output if name in ('main','romfs') else a.source)/(name+'.c');obj=a.output/(name+'.o');objects.append(obj)
        subprocess.run([str(a.cc),'-O2','-std=gnu11','-D_BSD_SOURCE','-D_POSIX_SOURCE','-D_POSIX_C_SOURCE=200112L','-D_DEFAULT_SOURCE','-D__USE_MINGW_ANSI_STDIO=1','-D_FILE_OFFSET_BITS=64','-I'+str(a.source),'-I'+str(a.source/'mbedtls/include'),'-c',str(src),'-o',str(obj)],check=True)
    # Build crypto dependencies with the same CRT/toolchain as the executable.
    # Reusing the installed archives can mix UCRT and MSVCRT FILE structures.
    makefile=(a.source/'mbedtls/library/Makefile').read_text().replace('\\\n',' ')
    archive=a.cc.with_name('ar.exe')
    groups={}
    for name,tag in (('crypto','CRYPTO'),('x509','X509'),('tls','TLS')):
        match=re.search(r'^OBJS_'+tag+r'\s*=([^\n]+)',makefile,re.M);assert match
        groups[name]=match[1].split()
    def compile_crypto(name):
        subprocess.run([str(a.cc),'-O2','-D_FILE_OFFSET_BITS=64','-I'+str(a.source/'mbedtls/include'),'-c',str(a.source/'mbedtls/library'/Path(name).with_suffix('.c')),'-o',str(a.output/('mbed-'+name))],check=True)
    with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(compile_crypto,sorted({n for group in groups.values() for n in group})))
    for name,group in groups.items():
        subprocess.run([str(archive),'rcs',str(a.output/('libmbed'+name+'.a')),*[str(a.output/('mbed-'+n)) for n in group]],check=True)
    subprocess.run([str(a.cc),'-o',str(a.output/'hacbrewpack-low-disk.exe'),*[str(x) for x in objects],'-static','-L'+str(a.output),'-lmbedtls','-lmbedx509','-lmbedcrypto'],check=True)
    print(a.output/'hacbrewpack-low-disk.exe')

if __name__=='__main__':main()
