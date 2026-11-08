"""GPL-3.0-or-later. Boot expanded storage and persistent Alpine in the real engine.

Requires a current sanitizer runtime and network fixture build. By default it
uses a locally cached, hash-pinned Alpine archive and makes no Internet requests
inside the guest. --online additionally exercises apk add and persistence.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tools.dev.embedded_linux import ROOT

ALPINE_SHA256 = '8d811b1aff11a6e40433c219142e06496a4f92dd06d86eefca58f62602ce3f24'
ALPINE_ARCHIVE = 'alpine-minirootfs-3.23.8-riscv64.tar.gz'


def build(runtime, network, images, archive_directory, output, online=False):
    manifest = json.loads((runtime / 'provenance.json').read_text())
    if not manifest.get('host_only') or not manifest.get('sanitizer'):
        raise ValueError('A host sanitizer runtime is required')
    for file, digest in manifest['adapter_sources'].items():
        if hashlib.sha256((ROOT / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale runtime: ' + file)
    for file, digest in manifest['patches'].items():
        if hashlib.sha256((ROOT / 'tooling/subsystem' / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale patch: ' + file)
    net = json.loads((network / 'provenance.json').read_text())
    for file, digest in net['sources'].items():
        path = ROOT / file
        if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Stale network: ' + file)
    archive = archive_directory / ALPINE_ARCHIVE
    if archive.is_symlink() or hashlib.sha256(archive.read_bytes()).hexdigest() != ALPINE_SHA256:
        raise ValueError('Pinned Alpine minirootfs is missing or changed')
    output.mkdir(parents=True, exist_ok=False)
    binary = output / 'test-large-guest'
    command = [os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O1', '-g', '-pthread',
               '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'backend/subsystem'),
               str(ROOT / 'tests/subsystem/test_large_data_guest.cpp'),
               *[str(ROOT / 'src/platform' / name) for name in
                 ('linux_data_disk.cpp', 'linux_shared_folder.cpp', 'directory.cpp')],
               *map(str, sorted(runtime.glob('engine-*.o'))), str(runtime / 'adapter.o'),
               *map(str, sorted(network.glob('network-*.o'))), '-o', str(binary)]
    subprocess.run(command, check=True, timeout=120)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1')
               + ':halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (output / 'acceptance.log').open('w') as log:
        subprocess.run([str(binary), str(images), str(archive_directory), 'online' if online else 'offline'],
                       check=True, timeout=600, env=env, stdout=log, stderr=subprocess.STDOUT)
    (output / 'result.json').write_text(json.dumps(dict(host_only=True, ps5_verified=False,
        online=online, alpine_archive_sha256=ALPINE_SHA256,
        images={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in images.glob('*.bin')},
        command=command), indent=2) + '\n')
    print('PASS: expanded guest storage, Alpine userland and restart persistence')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('runtime', 'network', 'images', 'archive-directory', 'output'):
        parser.add_argument('--' + option, required=True, type=Path)
    parser.add_argument('--online', action='store_true')
    args = parser.parse_args()
    build(args.runtime.resolve(), args.network.resolve(), args.images.resolve(),
          args.archive_directory.resolve(), args.output.resolve(), args.online)
