#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-compile owned SSH library; no app selection, deployment or network."""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tarfile
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/owned-ssh-native'
def build():
    if sys.version_info<(3,12): raise SystemExit('Python 3.12+ required')
    dependencies=runpy.run_path(str(ROOT/'tools/prepare-ssh.py'))['DEPENDENCIES']
    archives=[]
    for name,version,expected in dependencies:
        archive=ROOT/'.deps/ssh/downloads'/f'{name}.tar.gz'
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'{name}: source hash mismatch')
        archives.append((archive,BUILD/f'{name}-{version}'))
    BUILD.mkdir(parents=True,exist_ok=True)
    for archive,destination in archives:
        if destination.exists(): shutil.rmtree(destination)
        with tarfile.open(archive) as source: source.extractall(BUILD,filter='data')
    ssl=BUILD/'wolfssl-5.9.4-stable'; ssh=BUILD/'wolfssh-1.6.0-stable'
    subprocess.run(['patch','-p1','--fuzz=0','-i',str(ROOT/'tooling/ssh/ps5-wolfcrypt-portability.patch')],cwd=ssl,check=True)
    subprocess.run(['patch','-p1','--fuzz=0','-i',str(ROOT/'tooling/ssh/channel-exit-status.patch')],cwd=ssh,check=True)
    subprocess.run(['patch','-p1','--fuzz=0','-i',str(ROOT/'tooling/ssh/session-channel-limit.patch')],cwd=ssh,check=True)
    sftp='PS5TERM_OWNED_SFTP' in os.environ.get('APP_DEFINITIONS','').split()
    if sftp:
        for name in ('owned-sftp-bounds.patch','native-sftp-directory.patch'):
            subprocess.run(['patch','-p1','--fuzz=0','-i',str(ROOT/'tooling/ssh'/name)],cwd=ssh,check=True)
    flags=['-O2','-ffunction-sections','-fdata-sections','-DWOLFSSL_USER_SETTINGS',
           '-DPS5TERM_OWNED_SSH_NATIVE','-Dps5term_host_seed=ps5term_native_seed',
           f'-I{ROOT}',f'-I{ROOT}/backend/owned_ssh',f'-I{ssl}',f'-I{ssh}']
    if sftp:
        flags.append('-DPS5TERM_OWNED_SFTP')
        # Raw-syscall SDK functions would kill the app; see native_sftp_stat.h.
        flags += ['-Dchown=ps5term_sftp_chown','-Dfchown=ps5term_sftp_fchown']
    crypto=('aes asn coding cpuid curve25519 dh ecc ed25519 error fe_operations '
            'ge_operations hash hmac kdf logging memory random sha sha256 sha512 signature '
            'sp_int sp_c64 wc_encrypt wc_port wolfmath wolfentropy '
            'sha3 wc_mlkem wc_mlkem_poly').split()
    sources=[ssl/'wolfcrypt/src'/f'{name}.c' for name in crypto]
    sources += [ssh/'src'/f'{name}.c' for name in ('internal','io','log','ossh','port','ssh')]
    sources += [ROOT/'backend/owned_ssh'/name for name in
                ('native_entropy.c','identity.cpp','connection.cpp','service.cpp')]
    if sftp:
        sources += [ssh/'src/wolfsftp.c',ROOT/'backend/owned_ssh/native_sftp_directory.cpp',
                    ROOT/'backend/owned_ssh/native_sftp_stat.c']
    objects=[BUILD/f'{i}.o' for i in range(len(sources))]
    env={**os.environ,'PS5_PAYLOAD_SDK':str(ROOT/'.deps/native/ps5-payload-sdk')}
    def compile_one(pair):
        source,obj=pair
        args=['-std=c++20','-fno-exceptions','-fno-rtti'] if source.suffix=='.cpp' else ['-std=c11']
        subprocess.run(['sh',str(ROOT/'tooling/prospero-clang18'),*args,*flags,'-c',str(source),'-o',str(obj)],env=env,check=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(compile_one,zip(sources,objects)))
    archive=BUILD/'libowned-ssh.a';archive.unlink(missing_ok=True)
    subprocess.run(['llvm-ar-18','rcs',str(archive),*map(str,objects)],check=True)
    print(f'Native link input only: {archive.relative_to(ROOT)}')
if __name__=='__main__': build()
