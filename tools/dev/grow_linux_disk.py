#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Grow an OFFLINE PS5Term ext2 disk into a new file, preserving the original.

Stop Linux before downloading the source. This tool never contacts a console.
Requires e2fsprogs (brew install e2fsprogs on macOS).
"""
import argparse
import fcntl
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile

MIB = 1024 * 1024
MAX_MIB = 1024


def find_tool(name):
    found = shutil.which(name)
    if found:
        return found
    for prefix in ('/opt/homebrew/opt/e2fsprogs/sbin', '/usr/local/opt/e2fsprogs/sbin',
                   '/usr/sbin', '/sbin'):
        candidate = Path(prefix) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise FileNotFoundError(f'{name} missing: install e2fsprogs')


def run(arguments, allowed=(0,)):
    result = subprocess.run(arguments, capture_output=True, text=True)
    if result.returncode not in allowed:
        raise RuntimeError(f'{Path(arguments[0]).name} failed ({result.returncode}): '
                           + result.stdout + result.stderr)


def grow(source, output, size_mib):
    source, output = Path(source), Path(output)
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    if not isinstance(size_mib, int) or not 16 <= size_mib <= MAX_MIB:
        raise ValueError('Size must be between 16 and 1024 MiB')
    fsck, resize = find_tool('e2fsck'), find_tool('resize2fs')
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = os.fstat(fd)
        target = size_mib * MIB
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                before.st_size < 16 * MIB or before.st_size % 512 or
                target <= before.st_size):
            raise ValueError('Expected a regular unlinked offline disk and a larger target size')
        header = os.pread(fd, 60, 1024)
        if len(header) != 60 or header[56:58] != b'\x53\xef':
            raise ValueError('Source is not an ext2 disk image')
        blocks, = struct.unpack_from('<I', header, 4)
        log_size, = struct.unpack_from('<I', header, 24)
        if not blocks or log_size > 2 or blocks * (1024 << log_size) > before.st_size:
            raise ValueError('Invalid or truncated ext2 filesystem')
        # Same-directory private staging allows atomic, no-overwrite publication.
        with tempfile.TemporaryDirectory(prefix='.ps5term-grow-', dir=output.parent) as stage:
            candidate = Path(stage) / 'data.ext2'
            with os.fdopen(os.dup(fd), 'rb') as original, candidate.open('xb') as copy:
                os.chmod(candidate, 0o600)
                shutil.copyfileobj(original, copy, 1024 * 1024)
                copy.flush()
                os.fsync(copy.fileno())
            after = os.fstat(fd)
            if ((before.st_size, before.st_mtime_ns, before.st_ctime_ns) !=
                    (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
                raise RuntimeError('Source changed during copy; stop Linux and try again')
            # Correct recoverable errors in the COPY only; serious errors abort.
            run([fsck, '-f', '-p', str(candidate)], allowed=(0, 1))
            with candidate.open('r+b') as disk:
                disk.truncate(target)
            run([resize, str(candidate), f'{size_mib}M'])
            run([fsck, '-f', '-n', str(candidate)])
            with candidate.open('rb') as disk:
                os.fsync(disk.fileno())
            os.link(candidate, output)  # Fails rather than replacing an existing path.
            parent = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(parent)
            finally:
                os.close(parent)
    finally:
        os.close(fd)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--size-mib', required=True, type=int)
    args = parser.parse_args()
    try:
        result = grow(args.source, args.output, args.size_mib)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'Cannot grow disk: {error}\n')
    print(f'Created {result} ({args.size_mib} MiB). Original preserved; no console changed.')


if __name__ == '__main__':
    main()
