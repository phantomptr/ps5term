#!/usr/bin/env python3
"""Hash public build inputs; never include private captures, credentials or timestamps.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def generate(root, output, definitions=()):
    definitions = sorted(set(definitions))
    if any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?", item) for item in definitions):
        raise ValueError("Invalid compile definition")
    files = {}
    for name in ('src', 'backend', 'tools', 'tooling', 'sce_sys', 'assets', 'runtime', 'third_party'):
        directory = root / name
        if not directory.exists(): continue
        for path in sorted(directory.rglob('*')):
            relative = path.relative_to(root)
            if any(part.startswith('.') or part in ('__pycache__', 'build', 'bin', 'obj') for part in relative.parts):
                continue
            if path.is_symlink(): raise ValueError('Build inputs may not be symlinks: ' + str(relative))
            if path.is_file() and path.suffix != '.pyc':
                files[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ('Makefile', 'NOTICE.md'):
        path = root / name
        if path.is_file(): files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    identity = hashlib.sha256(json.dumps(dict(files=files, definitions=definitions), sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'build-info.json').write_text(json.dumps(dict(schema=1, build_id=identity, files=files, definitions=definitions), indent=2) + '\n')
    version = ''
    param = root / 'sce_sys' / 'param.json'
    if param.is_file():
        version = json.loads(param.read_text()).get('contentVersion', '')
    if not re.fullmatch(r'(\d{2}\.\d{3}\.\d{3})?', version):
        raise ValueError('param.json contentVersion must use NN.NNN.NNN')
    (output / 'build_identity.hpp').write_text('// Generated public-input identity.\n#pragma once\n#define PS5TERM_BUILD_ID "' + identity + '"\n'
                                              + ('#define PS5TERM_VERSION "' + version + '"\n' if version else ''))
    return identity

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--definition', action='append', default=[])
    args = parser.parse_args()
    print(generate(args.root.resolve(), args.output.resolve(), args.definition))
