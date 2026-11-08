"""Real guest DHCP/DNS/HTTP through the bounded libslirp profile, host only.
GPL-3.0-or-later. Localhost fixtures only; never contacts the Internet or a console.

Reuses a verified sanitizer runtime build (tools.dev.build_embedded_runtime) so
the engine objects match the current adapter sources, compiles the pinned
libslirp profile plus the owned network boundary with the same sanitizers, and
runs tests/subsystem/test_network_guest.c against the given images twice.
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
from tools.dev.prepare_slirp import prepared_source, SHA256, COMMIT


def build(runtime: Path, images: Path, output: Path):
    manifest = json.loads((runtime / 'provenance.json').read_text())
    if not manifest.get('host_only') or not manifest.get('sanitizer'):
        raise ValueError('A verified host sanitizer runtime is required')
    for file, digest in manifest['adapter_sources'].items():
        if hashlib.sha256((ROOT / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale runtime source: ' + file)
    for file, digest in manifest['patches'].items():
        if hashlib.sha256((ROOT / 'tooling/subsystem' / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale runtime patch: ' + file)
    for name in ('bbl64.bin', 'kernel-riscv64.bin', 'root-riscv64.bin'):
        if (images / name).is_symlink() or not (images / name).is_file():
            raise ValueError('Missing guest image: ' + name)
    if output.exists():
        raise FileExistsError('Choose a fresh output directory')
    output.mkdir(parents=True)
    source = prepared_source(ROOT) / 'src'
    owned = ROOT / 'backend/subsystem/net'
    cc = os.environ.get('HOST_CC', 'clang')
    flags = ['-std=gnu11', '-O1', '-g', '-pthread', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
             '-I' + str(owned), '-I' + str(source), '-I' + str(ROOT / 'backend/subsystem')]
    first = manifest['compile_commands'][0]
    if '-arch' in first:
        flags += ['-arch', first[first.index('-arch') + 1]]
    sources = [*sorted(source.glob('*.c')), *sorted(owned.glob('*.c'))]
    objects = []
    commands = []
    for index, path in enumerate(sources):
        obj = output / f'network-{index}.o'
        objects.append(str(obj))
        warnings = ['-Wall', '-Wextra', '-Werror'] if path.parent == owned else []
        command = [cc, *flags, *warnings, '-c', str(path), '-o', str(obj)]
        commands.append(command)
        subprocess.run(command, check=True, timeout=120)
    engine = [*sorted(runtime.glob('engine-*.o')), runtime / 'adapter.o']
    binary = output / 'test-network-guest'
    command = [cc, *flags, '-Wall', '-Wextra', '-Werror', '-DPS5TERM_BOUNDED_NAT',
               str(ROOT / 'tests/subsystem/test_network_guest.c'), *map(str, engine), *objects, '-o', str(binary)]
    commands.append(command)
    subprocess.run(command, check=True, timeout=120)
    log = output / 'acceptance.log'
    with log.open('w') as stream:
        subprocess.run([sys.executable, str(ROOT / 'tests/subsystem/run_network_guest.py'), str(binary), str(images)],
                       check=True, timeout=240, stdout=stream, stderr=subprocess.STDOUT)
    (output / 'provenance.json').write_text(json.dumps(dict(
        host_only=True, ps5_runtime_verified=False, runtime=str(runtime),
        runtime_manifest_sha256=hashlib.sha256((runtime / 'provenance.json').read_bytes()).hexdigest(),
        libslirp=dict(archive_sha256=SHA256, commit=COMMIT),
        images={name: hashlib.sha256((images / name).read_bytes()).hexdigest()
                for name in ('bbl64.bin', 'kernel-riscv64.bin', 'root-riscv64.bin')},
        sources={str(p.relative_to(ROOT)) if ROOT in p.parents else p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in [*sources, ROOT / 'tests/subsystem/test_network_guest.c', ROOT / 'tests/subsystem/run_network_guest.py']},
        commands=commands, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()), indent=2) + '\n')
    print(log.read_text()[-3000:])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path, help='Existing --sanitize build_embedded_runtime output')
    parser.add_argument('--images', type=Path, default=ROOT / 'assets/linux', help='Guest image directory (default: bundled assets)')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build(args.runtime.resolve(), args.images.resolve(), args.output.resolve())
