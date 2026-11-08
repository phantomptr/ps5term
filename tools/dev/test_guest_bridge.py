"""SSH-style guest sessions over the second console, with a real guest. Host only.
GPL-3.0-or-later. Reuses a verified build_embedded_runtime sanitizer build.
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
    if output.exists():
        raise FileExistsError('Choose a fresh output directory')
    output.mkdir(parents=True)
    binary = output / 'test-guest-bridge'
    engine = [*sorted(runtime.glob('engine-*.o')), runtime / 'adapter.o', runtime / 'session.o']
    flags = ['-std=c++20', '-O1', '-g', '-pthread', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
             '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'backend/subsystem')]
    subprocess.run([os.environ.get('HOST_CXX', 'clang++'), *flags, str(ROOT / 'tests/subsystem/test_guest_bridge.cpp'),
                    str(ROOT / 'src/platform/guest_agent.cpp'), *map(str, engine), '-o', str(binary)], check=True, timeout=120)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1') + ':halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary), str(images)], check=True, timeout=240, env=env)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--images', type=Path, default=ROOT / 'assets/linux')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build(args.runtime.resolve(), args.images.resolve(), args.output.resolve())
