#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Isolated Bash reference build or PS5 link audit. Does not package or deploy."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=('reference', 'ps5'))
    parser.add_argument('--source', type=Path, default=ROOT / 'build/bash-port/source')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    if not (source / 'configure').is_file():
        parser.error('Prepare sources using tools/prepare-bash.py first')
    output = (args.output or ROOT / 'build/bash-port' / args.target).resolve()
    if output.exists():
        parser.error('Output already exists; preserve it and select a fresh --output')
    output.mkdir(parents=True)
    environment = dict(os.environ, CONFIG_SITE='/dev/null')
    cc = os.environ.get('HOST_CC', 'clang-18' if shutil.which('clang-18') else 'clang')
    environment['CC_FOR_BUILD'] = cc
    options = ['--without-bash-malloc', '--disable-nls', '--disable-readline',
               '--disable-job-control', '--disable-net-redirections',
               '--prefix=/data/ps5term/usr']
    if args.target == 'ps5':
        options.append('--disable-multibyte')
        sdk = Path(os.environ.get('PS5_PAYLOAD_SDK', ROOT / '.deps/native/ps5-payload-sdk')).resolve()
        if not (sdk / 'target/lib/crt1.o').is_file():
            parser.error('Missing PS5 Payload SDK')
        q = shlex.quote
        # Reuse the SDK target/linker script with host-native LLVM tools.
        # The SDK crt is used only for this link audit, not an app launch adapter.
        (output / 'cc').write_text(f'''#!/bin/sh
sdk={q(str(sdk))}
link=yes
for arg do case "$arg" in -c|-E|-S|-v|--version|-dumpmachine|-dumpversion) link=no;; esac; done
if [ "$link" = yes ]; then
 set -- "$@" "$sdk/target/lib/crt1.o" -L"$sdk/target/lib" -lc -lkernel -lSceLibcInternal -lSceNet
fi
exec {q(cc)} -DPS5TERM_BASH_PORT=1 -Dfputs_unlocked=fputs -target x86_64-sie-ps5 -B{q(str(output))} -isysroot "$sdk" -isystem "$sdk/target/include" -fvisibility-nodllstorageclass=default -fno-stack-protector -fno-plt -femulated-tls "$@"
''')
        linker = os.environ.get('PS5_LD', 'ld.lld-18')
        (output / 'prospero-lld').write_text(f'''#!/bin/sh
exec {q(linker)} -m elf_x86_64 -pie -T{q(str(sdk / 'ldscripts/elf_x86_64.x'))} --eh-frame-hdr -z max-page-size=0x4000 "$@" --hash-style=gnu
''')
        for name in ('cc', 'prospero-lld'):
            (output / name).chmod(0o755)
        environment.update(CC=str(output / 'cc'), AR='llvm-ar-18', RANLIB='llvm-ranlib-18')
        build = subprocess.check_output(['sh', str(source / 'support/config.guess')], text=True).strip()
        options += [f'--build={build}', '--host=x86_64-unknown-freebsd11']
    else:
        environment['CC'] = cc
    for name, command in [('configure', ['sh', str(source / 'configure'), *options]),
                          ('build', ['make', '-j2', f'CC_FOR_BUILD={cc}'])]:
        print(f'{args.target}: {name} (log: {output / (name + ".log")})', flush=True)
        with (output / (name + '.log')).open('w') as log:
            subprocess.run(command, cwd=output, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    print(f'Built {output / "bash"}; PS5 runtime/SSH integration remains unverified.')


if __name__ == '__main__':
    main()
