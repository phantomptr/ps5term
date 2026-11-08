#!/usr/bin/env python3
"""Verify the vendored libvterm source pin before compiling it.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent / 'third_party/libvterm'
lock = json.loads((root / 'source.json').read_text())
if (lock.get('version') != '0.3.3' or lock.get('sha256') !=
        '09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0'):
    raise SystemExit('Unexpected libvterm source pin')
actual = {}
for path in sorted(root.rglob('*')):
    if path.is_symlink():
        raise SystemExit('Symlink in vendored libvterm')
    if path.is_file() and path.name != 'source.json':
        actual[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
if actual != lock.get('files'):
    raise SystemExit('Vendored libvterm files differ from the source manifest')
