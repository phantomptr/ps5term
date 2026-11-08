#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the isolated owned-SSH host prototype from verified source archives.

No network, payload preparation, native assets or installed app changes.
Use prepare-ssh.py --archives-only if the pinned archive cache is absent.
"""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build' / f'owned-ssh-host-{sys.platform}'


def build():
    if sys.version_info < (3, 12):
        raise SystemExit('Use Python 3.12+ for filtered source archive extraction')
    dependencies = runpy.run_path(str(ROOT / 'tools/prepare-ssh.py'))['DEPENDENCIES']
    BUILD.mkdir(parents=True, exist_ok=True)
    for name, version, expected in dependencies:
        archive = ROOT / '.deps/ssh/downloads' / f'{name}.tar.gz'
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'{name}: pinned source archive hash mismatch')
        destination = BUILD / f'{name}-{version}'
        if destination.exists():
            shutil.rmtree(destination)
        with tarfile.open(archive) as source:
            source.extractall(BUILD, filter='data')
    ssl = BUILD / 'wolfssl-5.9.4-stable'
    ssh = BUILD / 'wolfssh-1.6.0-stable'
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', str(ROOT / 'tooling/ssh/owned-sftp-bounds.patch')], cwd=ssh, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', str(ROOT / 'tooling/ssh/channel-exit-status.patch')], cwd=ssh, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', str(ROOT / 'tooling/ssh/session-channel-limit.patch')], cwd=ssh, check=True)
    config = ROOT / 'backend/owned_ssh'
    flags = ['-O2', '-D_DEFAULT_SOURCE=1', '-DWOLFSSL_USER_SETTINGS', '-DPS5TERM_OWNED_SFTP',
             f'-I{config}', f'-I{ssl}', f'-I{ssh}']
    # wolfCrypt files compile their own feature guards. Do not compile TLS,
    # hardware-specific ports, assembly or upstream example servers.
    crypto = ('aes asn coding cpuid curve25519 dh ecc ed25519 error fe_operations '
              'ge_operations hash hmac kdf logging memory random sha sha256 sha512 signature '
              'sp_int sp_c64 wc_encrypt wc_port wolfmath wolfentropy '
              'sha3 wc_mlkem wc_mlkem_poly').split()
    sources = [ssl / 'wolfcrypt/src' / f'{name}.c' for name in crypto]
    sources += [ssh / 'src' / f'{name}.c' for name in
                ('internal', 'io', 'log', 'ossh', 'port', 'ssh', 'wolfsftp')]
    sources += [ROOT / 'tests/ssh/host_entropy.c']
    objects = [BUILD / f'{i}.o' for i in range(len(sources))]

    def compile_one(pair):
        source, obj = pair
        subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11',
                        *flags, '-c', str(source), '-o', str(obj)], check=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(compile_one, zip(sources, objects)))
    archive = BUILD / 'libowned-ssh.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['ar', 'rcs', str(archive), *map(str, objects)], check=True)
    common = ['backend/owned_ssh/connection.cpp', 'src/ssh/command_session.cpp',
              'src/builtin_shell.cpp', 'src/platform/system_info.cpp', 'src/platform/filename_expansion.cpp', 'src/platform/disk_usage.cpp', 'src/platform/directory.cpp']
    identity = ['backend/owned_ssh/identity.cpp']
    service = ['backend/owned_ssh/service.cpp', *identity, *common]
    targets = {
        'host-server': ['tests/ssh/host_server.cpp', *common],
        'service-server': ['tests/ssh/service_server.cpp', *service],
        'identity-test': ['tests/ssh/test_identity.cpp', *identity],
        'identity-entropy-test': ['tests/ssh/test_identity_entropy_failure.cpp', *identity],
        'service-test': ['tests/ssh/test_service.cpp', *service],
    }
    for name, sources in targets.items():
        subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20',
                        '-Wall', '-Wextra', '-Werror', *flags, f'-I{ROOT}',
                        *[str(ROOT / path) for path in sources], str(archive),
                        '-o', str(BUILD / name)], check=True)
    vterm_objects = []
    for source in sorted((ROOT / 'third_party/libvterm/src').glob('*.c')) + [ROOT / 'src/terminal_memory.c']:
        vterm_object = BUILD / f'ui-{source.stem}.o'
        subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-O1',
                        f'-I{ROOT / "third_party/libvterm/include"}', '-c', str(source), '-o', str(vterm_object)], check=True)
        vterm_objects.append(str(vterm_object))
    ui_sources = ['tests/ssh/test_password_settings.cpp', 'src/app.cpp', 'src/terminal.cpp',
                  'src/telnet.cpp', 'src/connection.cpp', 'src/ssh_service_owned.cpp',
                  'src/settings.cpp', 'src/pointer.cpp', 'src/diagnostics.cpp',
                  'src/terminal_model.cpp', 'src/terminal_transport.cpp', 'src/scrollback.cpp',
                  'src/platform/launcher.cpp', 'src/platform/pty_launcher.cpp', 'src/platform/guest_agent.cpp',
                  'src/platform/linux_subsystem.cpp', *service]
    host_pty = BUILD / 'ui-pty.o'
    subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-O1', f'-I{ROOT / "src"}',
                    '-c', str(ROOT / 'src/platform/pty.c'), '-o', str(host_pty)], check=True)
    vterm_objects += [str(host_pty)]
    ui_binary = BUILD / 'password-settings-test'
    subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20',
                    '-Wall', '-Wextra', '-Werror', *flags, f'-I{ROOT}', '-Isrc',
                    '-DPS5TERM_OWNED_SSH', '-ffunction-sections', '-fdata-sections',
                    *[str(ROOT / path) for path in ui_sources], *vterm_objects, str(archive),
                    '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
                    '-o', str(ui_binary)], check=True)
    subprocess.run([str(ui_binary)], check=True, timeout=15)
    for name in ('identity-test' , 'identity-entropy-test', 'service-test'):
        subprocess.run([str(BUILD / name)], check=True, timeout=15)
    entropy = BUILD / 'native-entropy.o'
    subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-D_DEFAULT_SOURCE=1', '-Wall', '-Wextra', '-Werror',
                    '-Dopen=test_open', '-Dread=test_read', '-Dclose=test_close', '-Dfstat=test_fstat', '-c',
                    str(ROOT / 'backend/owned_ssh/native_entropy.c'), '-o', str(entropy)], check=True)
    subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-D_DEFAULT_SOURCE=1', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'tests/ssh/test_native_entropy.c'), str(entropy),
                    '-o', str(BUILD / 'native-entropy-test')], check=True)
    subprocess.run([str(BUILD / 'native-entropy-test')], check=True, timeout=15)
    stat_object = BUILD / 'native-sftp-stat.o'
    subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-D_DEFAULT_SOURCE=1',
                    '-Dlstat=test_missing_lstat', '-I'+str(ROOT), '-c',
                    str(ROOT/'backend/owned_ssh/native_sftp_stat.c'), '-o', str(stat_object)], check=True)
    subprocess.run([os.environ.get('HOST_CC', 'clang'), '-std=c11', '-D_DEFAULT_SOURCE=1',
                    '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT),
                    str(ROOT/'tests/ssh/test_native_sftp_stat.c'), str(stat_object),
                    '-o', str(BUILD/'native-sftp-stat-test')], check=True)
    subprocess.run([str(BUILD/'native-sftp-stat-test')], check=True, timeout=5)
    print('Built and tested isolated owned SSH host prototype (not a PS5 server)')


if __name__ == '__main__':
    build()
