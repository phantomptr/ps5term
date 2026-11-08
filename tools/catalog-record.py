#!/usr/bin/env python3
# PS5Term - prepare the ProsperoStore catalog record from the actual ZIP.
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print a proposed record; does not publish a release or contact the catalog."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
param = json.loads((root / 'sce_sys/param.json').read_text())
version = param['contentVersion']
artifact = root / 'dist' / f'PS5Term-{version}.zip'
if not artifact.is_file():
    raise SystemExit('Build the release ZIP first: make release')
repo = 'https://github.com/phantomptr/ps5term'
record = {
    'titleid': param['titleId'],
    'name': 'PS5Term',
    'kind': 'app',
    'category': 'utilities',
    'description': 'Terminal for jailbroken PS5: a built-in shell, an SSH and SFTP server, and optional Alpine Linux. Works with a controller, keyboard or mouse.',
    'license': 'GPL-3.0-or-later',
    'author': 'phantomptr',
    'version': version,
    'source_repo': repo,
    'artifact_url': f'{repo}/releases/download/{version}/{artifact.name}',
    'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
    'icon_url': f'https://raw.githubusercontent.com/phantomptr/ps5term/{version}/sce_sys/icon0.png',
}
print(json.dumps(record, indent=2))
