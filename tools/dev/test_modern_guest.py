# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a modern guest through the actual owned interpreter, host-only.

Requires an existing verified build_embedded_runtime host build. It does not
start QEMU, modify a console, or treat candidate metadata as runtime evidence.
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


def run_acceptance(runtime: Path, images: Path, output: Path):
    metadata = json.loads((runtime / 'provenance.json').read_text())
    if not metadata.get('host_only') or metadata.get('thread_sanitizer'):
        raise ValueError('Use a verified ordinary or ASan/UBSan host runtime')
    for file, digest in metadata['adapter_sources'].items():
        if hashlib.sha256((ROOT / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale runtime adapter: ' + file)
    for file, digest in metadata['patches'].items():
        if hashlib.sha256((ROOT / 'tooling/subsystem' / file).read_bytes()).hexdigest() != digest:
            raise ValueError('Stale runtime engine patch: ' + file)
    objects = [*sorted(runtime.glob('engine-*.o')), runtime / 'adapter.o']
    before = {name: hashlib.sha256((images / name).read_bytes()).hexdigest()
              for name in ('bbl64.bin', 'kernel-riscv64.bin', 'root-riscv64.bin')}
    if output.exists():
        raise FileExistsError('Preserve previous acceptance evidence')
    output.mkdir(parents=True)
    flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fno-omit-frame-pointer',
             '-I' + str(ROOT / 'backend/subsystem')]
    if metadata.get('sanitizer'):
        flags += ['-fsanitize=address,undefined']
    first = metadata['compile_commands'][0]
    if '-arch' in first:
        flags += ['-arch', first[first.index('-arch') + 1]]
    binary = output / 'test-modern-guest'
    source = ROOT / 'tests/subsystem/test_modern_guest.c'
    command = [os.environ.get('HOST_CC', 'clang'), *flags, str(source), *map(str, objects), '-o', str(binary)]
    subprocess.run(command, check=True, timeout=60)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1') + ':halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    timed_out = False
    with (output / 'acceptance.log').open('w') as log:
        try:
            result = subprocess.run([str(binary), str(images)], env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=240)
            returncode = result.returncode
        except subprocess.TimeoutExpired:
            # subprocess.run kills and reaps the test before raising. Preserve
            # failed evidence even if a defective interpreter step never returns.
            timed_out = True
            returncode = None
    unchanged = all(hashlib.sha256((images / name).read_bytes()).hexdigest() == digest for name, digest in before.items())
    evidence = dict(schema=1, host_only=True, ps5_runtime_verified=False,
                    passed=returncode == 0 and unchanged, timed_out=timed_out,
                    returncode=returncode, image_hashes=before, images_unchanged=unchanged,
                    runtime_manifest_sha256=hashlib.sha256((runtime / 'provenance.json').read_bytes()).hexdigest(),
                    test_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), compile_command=command)
    (output / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    if not evidence['passed']:
        raise RuntimeError('Modern guest acceptance failed; inspect ' + str(output / 'acceptance.log'))
    print((output / 'acceptance.log').read_text()[-5000:])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--images', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    run_acceptance(args.runtime.resolve(), args.images.resolve(), args.output.resolve())
