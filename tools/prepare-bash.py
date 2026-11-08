#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare pinned Bash sources for an isolated port; never install/deploy them."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def verified(records, cache, fetch=False):
    cache.mkdir(parents=True, exist_ok=True)
    paths = []
    for record in records:
        url = record['url']
        if not url.startswith('https://ftp.gnu.org/gnu/bash/'):
            raise RuntimeError('Unexpected source origin')
        path = cache / url.rsplit('/', 1)[1]
        if not path.exists() and fetch:
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != record['sha256']:
                raise RuntimeError(f'SHA256 mismatch: {path.name}')
            with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
                output.write(data)
                pending = Path(output.name)
            pending.replace(path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
            raise RuntimeError(f'SHA256 mismatch: {path.name}')
        paths.append(path.resolve())
    return paths


def prepare(records, cache, destination, fetch=False):
    paths = verified(records, cache, fetch)
    if destination.exists():
        raise RuntimeError(f'Source destination already exists: {destination}; preserve or move it')
    if sys.version_info < (3, 12):
        raise RuntimeError('Python 3.12+ required for filtered archive extraction')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as directory:
        work = Path(directory)
        with tarfile.open(paths[0]) as archive:
            archive.extractall(work, filter='data')
        source = work / 'bash-5.3'
        for patch in paths[1:]:
            with patch.open('rb') as stream:
                subprocess.run(['patch', '--batch', '--fuzz=0', '-p0'],
                               cwd=source, stdin=stream, check=True)
        with (ROOT / 'tooling/bash/ps5-portability.patch').open('rb') as stream:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1'],
                           cwd=source, stdin=stream, check=True)
        source.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='Download missing pinned GNU sources')
    parser.add_argument('--archives-only', action='store_true')
    parser.add_argument('--destination', type=Path, default=ROOT / 'build/bash-port/source')
    args = parser.parse_args()
    records = json.loads((ROOT / 'tooling/bash/sources.json').read_text())
    cache = ROOT / '.deps/shell/downloads'
    if args.archives_only:
        verified(records, cache, args.fetch)
    else:
        print(prepare(records, cache, args.destination.resolve(), args.fetch))


if __name__ == '__main__':
    main()
