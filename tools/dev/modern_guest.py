# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned modern guest source preparation. Linux build host only; never deploys.

Absolute links in an unused OpenRC target skeleton are data, not extraction
paths. Validate the entire archive, write all regular files first, then links.
No archive member may be below a link, even if the link appears later.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import sys
import tarfile

VERSION = '2025.02.19'
SHA256 = 'e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c'
PREFIX = 'buildroot-' + VERSION
ABSOLUTE_LINKS = {
    PREFIX + '/package/skeleton-init-openrc/skeleton/etc/runlevels/default/sysv-rcs': '/etc/init.d/sysv-rcs',
}


def extract_source(archive: Path, destination: Path):
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        if len(members) > 25000 or sum(m.size for m in members) > 64 * 1024 * 1024:
            raise ValueError('Source expansion budget exceeded')
        names = {}
        links = set()
        for item in members:
            name = PurePosixPath(item.name)
            if (name.is_absolute() or '..' in name.parts or '\\' in item.name or
                not name.parts or name.parts[0] != PREFIX or (str(name) in names and not (item.isdir() and names[str(name)])) or
                not (item.isfile() or item.isdir() or item.issym())):
                raise ValueError('Unsafe archive member: ' + item.name)
            names[str(name)] = item.isdir()
            if item.issym():
                links.add(name)
                target = item.linkname
                resolved = posixpath.normpath(posixpath.join(str(name.parent), target))
                if ('\\' in target or not target or
                    (target.startswith('/') and ABSOLUTE_LINKS.get(str(name)) != target) or
                    (not target.startswith('/') and not resolved.startswith(PREFIX + '/'))):
                    raise ValueError('Unsafe archive link: ' + item.name)
        for item in members:
            if any(parent in links for parent in PurePosixPath(item.name).parents):
                raise ValueError('Archive member below link: ' + item.name)
        destination.mkdir(parents=True)
        try:
            for item in members:
                if item.issym():
                    continue
                path = destination.joinpath(*PurePosixPath(item.name).parts)
                if item.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with tar.extractfile(item) as source, path.open('xb') as target:
                        shutil.copyfileobj(source, target, 65536)
                    path.chmod(0o755 if item.mode & 0o111 else 0o644)
            for item in members:
                if item.issym():
                    path = destination.joinpath(*PurePosixPath(item.name).parts)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.symlink_to(item.linkname)
        except BaseException:
            shutil.rmtree(destination)
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('Use a disposable Linux build directory, not the macOS worktree')
    if args.archive.stat().st_size > 8 * 1024 * 1024:
        parser.error('Buildroot download budget exceeded')
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != SHA256:
        parser.error('Buildroot archive does not match the pinned source')
    extract_source(args.archive, args.destination)
    print(args.destination / PREFIX)
