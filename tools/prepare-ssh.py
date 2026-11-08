#!/usr/bin/env python3
# PS5Term - verified SSH dependencies. SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch pinned sources and prepare a separate build tree; never edit vendors."""
import hashlib
import argparse
import shutil
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.deps/ssh'
DEPENDENCIES = (
    ('wolfssl', '5.9.4-stable',
     '7256bfc89b183a75183806c7debfa203443873b0b4a562e1b80d68e01b45ac57'),
    ('wolfssh', '1.6.0-stable',
     'e345b580643af6715a4293ed7bab4e99ea562bb379adb8898eeae55c38b12f56'),
)


def prepare(archives_only=False):
    (CACHE / 'downloads').mkdir(parents=True, exist_ok=True)
    for name, version, expected in DEPENDENCIES:
        archive = CACHE / 'downloads' / f'{name}.tar.gz'
        if not archive.exists():
            temporary = archive.with_suffix('.download')
            url = f'https://codeload.github.com/wolfSSL/{name}/tar.gz/refs/tags/v{version}'
            with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as output:
                shutil.copyfileobj(response, output)
            temporary.replace(archive)
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f'{archive.name}: SHA256 mismatch; remove the cached archive and retry')
        if archives_only:
            print(f'Verified {name} {version} archive: {actual}')
            continue
        # Re-extract verified sources so stale edits cannot silently enter a build.
        destination = CACHE / f'{name}-{version}'
        if destination.exists():
            shutil.rmtree(destination)
        with tarfile.open(archive) as source:
            source.extractall(CACHE, filter='data')
        print(f'Verified {name} {version}: {actual}')

    if archives_only:
        return
    build = ROOT / 'build/ssh'
    if build.exists():
        shutil.rmtree(build)
    shutil.copytree(ROOT / 'third_party/ps5sshd', build)
    shsrv = build / 'shsrv'
    shutil.copytree(ROOT / 'third_party/shsrv', shsrv)
    makefile = build / 'Makefile'
    text = makefile.read_text()
    text = text.replace('SHSRV_BUNDLES := core http2_get launch suspend hbldr pkg_install',
                        'SHSRV_BUNDLES := core')
    makefile.write_text(text)
    # Identify our bundled server and leave separately installed ps5sshd alone.
    path = build / 'src/session.c'
    text = path.read_text().replace(
        '  wolfSSH_SetUserAuth(ctx, auth_cb);',
        '  wolfSSH_CTX_SetSshProtoIdStr(ctx, "SSH-2.0-PS5Term_1.0\\r\\n");\n'
        '  wolfSSH_SetUserAuth(ctx, auth_cb);')
    path.write_text(text)
    path = build / 'src/main.c'
    text = path.read_text().replace('#include <signal.h>', '#include <signal.h>\n#include <poll.h>')
    text = text.replace('#define PROC_NAME "ps5sshd.elf"', '#define PROC_NAME "ps5term-sshd"')
    text = text.replace('    if((fd=accept(srvfd,',
        '    if(access("/data/ps5term/ssh.enabled", F_OK) != 0) { break; }\n'
        '    struct pollfd listener = {srvfd, POLLIN, 0};\n'
        '    if(poll(&listener, 1, 500) <= 0) { reap_sessions(); continue; }\n'
        '    if((fd=accept(srvfd,')
    text = text.replace('  while(1) {\n    serve(notify_user);',
        '  while(access("/data/ps5term/ssh.enabled", F_OK) == 0) {\n    serve(notify_user);')
    # Terminate the server-owned SSH sessions when the setting is switched off.
    end = text.rfind('  return 0;')
    text = text[:end] + ('  for(int i=0; i<MAX_SESSIONS; i++) {\n'
                        '    if(g_sessions[i].pid > 0) { kill(g_sessions[i].pid, SIGTERM); }\n'
                        '  }\n') + text[end:]
    path.write_text(text)
    # Stable compilation metadata, keeping upstream source untouched.
    for path in build.rglob('*.c'):
        text = path.read_text().replace('__DATE__', '"2026-10-31"').replace('__TIME__', '"00:00:00"')
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives-only', action='store_true',
                        help='fetch/verify archives without touching sources or legacy build')
    prepare(archives_only=parser.parse_args().archives_only)
