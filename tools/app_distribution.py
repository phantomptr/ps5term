#!/usr/bin/env python3
"""Stage only the selected app's runtime assets. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

PAYLOADS=('assets/backend/ps5sshd.elf','assets/backend/ps5term-sh.elf')
def profile(app):
    metadata=json.loads((app/'assets/build-info.json').read_text())
    definitions=metadata.get('definitions')
    if metadata.get('schema')!=1 or not isinstance(definitions,list) or any(not isinstance(x,str) for x in definitions):
        raise ValueError('Missing or invalid app build profile')
    definitions=set(definitions)
    if 'PS5TERM_LINUX_SHARED_FOLDER' in definitions and 'PS5TERM_LINUX_SUBSYSTEM' not in definitions:
        raise ValueError('Shared folder requires the Linux subsystem')
    if 'PS5TERM_LINUX_PERSISTENCE' in definitions and 'PS5TERM_LINUX_SUBSYSTEM' not in definitions:
        raise ValueError('Persistent Linux storage requires the Linux subsystem')
    if 'PS5TERM_LINUX_NETWORK' in definitions and 'PS5TERM_LINUX_SUBSYSTEM' not in definitions:
        raise ValueError('Guest networking requires the Linux subsystem')
    owned='PS5TERM_OWNED_SSH' in definitions
    if 'PS5TERM_OWNED_SFTP' in definitions and not owned:
        raise ValueError('SFTP requires the owned SSH profile')
    required=set(PAYLOADS)
    if owned:
        required.remove(PAYLOADS[0])
        if 'PS5TERM_LEGACY_SHELL_RESEARCH' not in definitions:required.remove(PAYLOADS[1])
    return required,'OWNED_SSH.md' if owned else 'SSH.md'

def linux_profile(app):
    return 'PS5TERM_LINUX_SUBSYSTEM' in json.loads((app/'assets/build-info.json').read_text())['definitions']

def persistence_profile(app):
    return 'PS5TERM_LINUX_PERSISTENCE' in json.loads((app/'assets/build-info.json').read_text())['definitions']

def network_profile(app):
    return 'PS5TERM_LINUX_NETWORK' in json.loads((app/'assets/build-info.json').read_text())['definitions']

def verify_linux(root, directory, require_seed=False, require_network=False):
    metadata=json.loads((root/'tooling/subsystem/guest-images.json').read_text())
    if require_seed and 'data-seed.ext2' not in metadata['images']:
        raise ValueError('Persistent Linux profile requires a pinned data seed')
    if require_network:
        # The guest kernel must carry the virtio NIC and the libslirp notices must ship.
        if metadata.get('network') is not True:
            raise ValueError('Network profile requires network-capable pinned guest images')
        for name in ('LICENSE','COPYRIGHT'):
            notice=root/'assets/linux/licenses/libslirp-4.9.7'/name
            if notice.is_symlink() or not notice.is_file() or not notice.stat().st_size:
                raise ValueError('Network profile requires the bundled libslirp notice: '+name)
    for name,item in metadata['images'].items():
        if name not in ('bbl64.bin','kernel-riscv64.bin','root-riscv64.bin','data-seed.ext2'):
            raise ValueError('Unexpected Linux image name')
        path=directory/name
        if path.is_symlink() or not path.is_file() or path.stat().st_size!=item['bytes']:
            raise ValueError('Missing, linked or wrong-sized Linux image: '+name)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('Linux image hash mismatch: '+name)
    return metadata

def stage_assets(root,app):
    required,_=profile(app)
    linux=linux_profile(app)
    if linux:verify_linux(root,root/'assets/linux',persistence_profile(app),network_profile(app))
    source=root/'assets' 
    if not source.exists():return
    def ignore(directory,names):
        if Path(directory)==source and not linux:
            names=[name for name in names if name!='linux']
            return ['linux']+[name for name in names if (Path(directory)/name).relative_to(root).as_posix() in set(PAYLOADS)-required]
        return [name for name in names if (Path(directory)/name).relative_to(root).as_posix() in set(PAYLOADS)-required]
    shutil.copytree(source,app/'assets',dirs_exist_ok=True,ignore=ignore)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--app',type=Path,required=True)
    args=parser.parse_args();stage_assets(args.root,args.app)
