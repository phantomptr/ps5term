# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect and stage bounded TinyEMU guest candidates, never release or deploy.

Header checks catch build/config mistakes; they are not a boot or trust proof.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

SPECS = {
    'bbl64.bin': ('fw_jump.bin', 1024 * 1024),
    'kernel-riscv64.bin': ('Image', 32 * 1024 * 1024),
    'root-riscv64.bin': ('rootfs.ext2', 32 * 1024 * 1024),
}


def inspect_images(directory: Path):
    result = {}
    for name, (source, limit) in SPECS.items():
        path = directory / source
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= limit:
            raise ValueError('Missing, linked or oversized guest image: ' + source)
        with path.open('rb') as image:
            data = image.read(limit + 1)
        if not 0 < len(data) <= limit:
            raise ValueError('Guest image changed during inspection: ' + source)
        if name == 'bbl64.bin' and data.startswith(b'\x7fELF'):
            raise ValueError('TinyEMU needs the raw OpenSBI binary, not ELF')
        if name == 'kernel-riscv64.bin':
            if len(data) < 64 or data[56:60] != b'RSC\x05':
                raise ValueError('Expected a modern RISC-V Linux raw Image header')
            offset, size, flags = struct.unpack_from('<QQQ', data, 8)
            if offset != 0x200000 or not 0 < size <= limit or flags & 1:
                raise ValueError('Unsupported kernel load offset, size or endianness')
        if name == 'root-riscv64.bin' and (len(data) % 512 or data[1080:1082] != b'\x53\xef'):
            raise ValueError('Expected a sector-aligned ext2 root image')
        result[name] = dict(source=source, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    seed=directory/'data-seed.ext2'
    if seed.exists() or seed.is_symlink():
        if seed.is_symlink() or not seed.is_file() or seed.stat().st_size!=16*1024*1024:
            raise ValueError('Expected a regular 16 MiB data seed')
        data=seed.read_bytes()
        if len(data)!=16*1024*1024 or data[1080:1082]!=b'\x53\xef':
            raise ValueError('Expected an ext2 data seed')
        result['data-seed.ext2']=dict(source='data-seed.ext2',bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    result['bbl64.bin']['load_address'] = '0x80000000'
    result['kernel-riscv64.bin']['load_address'] = '0x80200000'
    return result


def stage(directory: Path, destination: Path):
    metadata = inspect_images(directory)
    if destination.exists() or destination.is_symlink():
        raise FileExistsError('Preserve existing guest candidate: ' + str(destination))
    destination.mkdir(parents=True)
    try:
        for name, item in metadata.items():
            shutil.copyfile(directory / item['source'], destination / name)
            if hashlib.sha256((destination / name).read_bytes()).hexdigest() != item['sha256']:
                raise ValueError('Guest source changed while staging')
        manifest = dict(schema=1, candidate_only=True, host_boot_verified=False,
                        ps5_runtime_verified=False, network=False, host_share=False,
                        disk_persistence=False, ram_mib=128, heap_limit_mib=160, images=metadata)
        (destination / 'guest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    except BaseException:
        shutil.rmtree(destination)
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--images', required=True, type=Path)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    stage(args.images, args.destination)
    print(args.destination / 'guest.json')
