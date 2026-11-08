"""Bounded, host-only TinyEMU Linux spike. Never deploys or contacts a console.
GPL-3.0-or-later. Sample guest is old research data, not a release image.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[2]
ARCHIVES={
    'tinyemu-2019-12-21.tar.gz': 'be8351f2121819b3172fcedce5cb1826fa12c87da1b7ed98f269d3e802a05555',
    'diskimage-linux-riscv-2018-09-23.tar.gz': '808ecc1b32efdd76103172129b77b46002a616dff2270664207c291e4fde9e14',
}

def verify_archive(path: Path, expected: str):
    if path.stat().st_size>64*1024*1024: raise ValueError('Archive download budget exceeded')
    if hashlib.sha256(path.read_bytes()).hexdigest()!=expected: raise ValueError('Pinned archive hash mismatch')

def extract_checked(archive: Path, destination: Path, max_bytes=64*1024*1024):
    if destination.exists() or destination.is_symlink(): raise FileExistsError(destination)
    with tarfile.open(archive) as source:
        members=[];total=0;names=set()
        for entry in source:
            name=PurePosixPath(entry.name)
            if (not entry.name or name.is_absolute() or '..' in name.parts or '\\' in entry.name or
                    not (entry.isfile() or entry.isdir()) or entry.name in names):
                raise ValueError('Unsafe source archive member')
            total+=entry.size;names.add(entry.name);members.append(entry)
            if len(members)>256 or total>max_bytes or entry.size>32*1024*1024:
                raise ValueError('Archive expansion budget exceeded')
        destination.mkdir(parents=True)
        try:
            # Links/devices rejected above. Write fresh regular files explicitly;
            # never restore archive owners, permissions or executable special bits.
            for entry in members:
                path=destination.joinpath(*PurePosixPath(entry.name).parts)
                if entry.isdir(): path.mkdir(parents=True,exist_ok=True); continue
                path.parent.mkdir(parents=True,exist_ok=True)
                with source.extractfile(entry) as src, path.open('xb') as dst:
                    shutil.copyfileobj(src,dst,64*1024)
        except BaseException:
            shutil.rmtree(destination)
            raise

def guest_config(images: Path):
    return dict(version=1,machine='riscv64',memory_size=128,
                bios=str(images/'bbl64.bin'),kernel=str(images/'kernel-riscv64.bin'),
                cmdline='console=hvc0 root=/dev/vda rw',drive0=dict(file=str(images/'root-riscv64.bin')))

def prepare(download=False):
    cache=ROOT/'.deps/subsystem/downloads';cache.mkdir(parents=True,exist_ok=True)
    for name,digest in ARCHIVES.items():
        path=cache/name
        if not path.exists():
            if not download: raise ValueError('Missing archive; explicitly pass --download')
            request=urllib.request.Request('https://bellard.org/tinyemu/'+name)
            temporary=path.with_suffix('.partial')
            try:
                with urllib.request.urlopen(request,timeout=30) as src, temporary.open('xb') as dst:
                    remaining=64*1024*1024
                    while True:
                        data=src.read(min(65536,remaining+1))
                        if not data:break
                        remaining-=len(data)
                        if remaining<0: raise ValueError('Download budget exceeded')
                        dst.write(data)
                verify_archive(temporary,digest);temporary.rename(path)
            finally: temporary.unlink(missing_ok=True)
        verify_archive(path,digest)
    return cache

def build(output: Path, download=False):
    cache=prepare(download)
    if output.exists(): raise FileExistsError('Preserve existing build; choose a fresh --output')
    source_root=output/'source';image_root=output/'images'
    extract_checked(cache/'tinyemu-2019-12-21.tar.gz',source_root)
    extract_checked(cache/'diskimage-linux-riscv-2018-09-23.tar.gz',image_root)
    source=source_root/'tinyemu-2019-12-21'; images=image_root/'diskimage-linux-riscv-2018-09-23'
    subprocess.run(['patch','-p1','--fuzz=0','-i',str(ROOT/'tooling/subsystem/tinyemu-host-profile.patch')],cwd=source,check=True)
    command=['make','-j2','temu','CONFIG_FS_NET=','CONFIG_SDL=','CONFIG_X86EMU=',
             'CONFIG_SLIRP=','CONFIG_INT128=','CC='+os.environ.get('HOST_CC','clang')]
    if sys.platform=='darwin':command.append('EMU_LIBS=')
    with (output/'build.log').open('w') as log:
        subprocess.run(command,cwd=source,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    config=output/'guest.cfg';config.write_text(json.dumps(guest_config(images),indent=2)+'\n')
    provenance=dict(schema=1,host_only=True,ps5_runtime_verified=False,archives=ARCHIVES,
                    host_patch_sha256=hashlib.sha256((ROOT/'tooling/subsystem/tinyemu-host-profile.patch').read_bytes()).hexdigest(),
                    source_url='https://bellard.org/tinyemu/',hash_origin='Observed official HTTPS downloads; not publisher signatures',
                    build_command=command,network=False,host_share=False,kvm=False,ram_mib=128)
    (output/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print('Built host-only emulator:',source/'temu')
    print('Guest config:',config)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download',action='store_true')
    parser.add_argument('--output',type=Path,default=ROOT/'build'/('embedded-linux-'+sys.platform))
    args=parser.parse_args()
    build(args.output.resolve(),args.download)
