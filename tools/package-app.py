#!/usr/bin/env python3
# PS5Term - installable app archive. SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import shutil
import zipfile
from pathlib import Path
from app_distribution import PAYLOADS, profile, linux_profile, verify_linux, persistence_profile, network_profile
root = Path(__file__).resolve().parents[1]
param = json.loads((root / 'sce_sys/param.json').read_text())
app = root / 'dist' / param['titleId']
payloads,readme=profile(app)
if linux_profile(app):verify_linux(root,app/'assets/linux',persistence_profile(app),network_profile(app))
for unused in set(PAYLOADS)-payloads:
    if (app/unused).exists():
        raise SystemExit(f'Unused payload in selected profile; rebuild app: {unused}')
for relative in ('eboot.bin', 'sce_sys/param.json', 'sce_sys/icon0.png',
                 'sce_module/libc.prx',*sorted(payloads)):
    if not (app / relative).is_file():
        raise SystemExit(f'Missing required app file: {relative}')
shutil.copy2(root / 'docs' / readme, app / 'README.md')
(app/'docs').mkdir(exist_ok=True)
for name in ('SSH.md','OWNED_SSH.md','TERMINAL_CLIENTS.md','SSH-SOURCES.md','FEATURES.md','SUBSYSTEM.md'):
    shutil.copy2(root/'docs'/name,app/'docs'/name)
shutil.copy2(root / 'LICENSE', app / 'LICENSE')
shutil.copy2(root / 'NOTICE.md', app / 'NOTICE.md')
output = app.parent / f"PS5Term-{param['contentVersion']}.zip"
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(p for p in app.rglob('*') if p.is_file()):
        item = zipfile.ZipInfo(path.relative_to(app.parent).as_posix(),
                               date_time=(2026, 10, 31, 0, 0, 0))
        item.compress_type = zipfile.ZIP_DEFLATED
        item.external_attr = 0o100644 << 16
        archive.writestr(item, path.read_bytes())
output.with_suffix('.zip.sha256').write_text(
    f'{hashlib.sha256(output.read_bytes()).hexdigest()}  {output.name}\n')
print(f'Installable homebrew app: {output} (experimental; see README for validation limits)')
