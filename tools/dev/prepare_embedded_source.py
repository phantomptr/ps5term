"""Prepare the pinned embedded interpreter; never relies on an old host build.
GPL-3.0-or-later. No image download, app deployment or console access.
"""
from __future__ import annotations
import errno
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from tools.dev.embedded_linux import ROOT, ARCHIVES, verify_archive, extract_checked

def source_identity(archive, patches):
    return hashlib.sha256(json.dumps(dict(archive=archive,patches=patches),
                         sort_keys=True,separators=(',',':')).encode()).hexdigest()

def tree_hashes(root):
    files={}
    for p in sorted(root.rglob('*')):
        if p.is_symlink():raise ValueError('Prepared source contains a symlink')
        if p.is_file():files[p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
        elif not p.is_dir():raise ValueError('Prepared source contains a special file')
    return files

def validate_tree(root, manifest):
    if root.is_symlink() or not root.is_dir() or tree_hashes(root)!=manifest:
        raise ValueError('Prepared source changed; preserve and inspect the cache before rebuilding')

def prepare_source(root=ROOT):
    name='tinyemu-2019-12-21.tar.gz'
    archive=root/'.deps/subsystem/downloads'/name
    verify_archive(archive,ARCHIVES[name])
    patches=[root/'tooling/subsystem'/n for n in
             ('tinyemu-host-profile.patch','tinyemu-embedded-profile.patch')]
    pins={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches}
    key=source_identity(ARCHIVES[name],pins)
    cache=root/'.deps/subsystem/prepared';cache.mkdir(parents=True,exist_ok=True)
    target=cache/key
    if not target.exists():
        with tempfile.TemporaryDirectory(prefix='preparing-',dir=cache) as tmp:
            staging=Path(tmp)/'prepared'
            extract_checked(archive,staging/'source')
            source=staging/'source/tinyemu-2019-12-21'
            for patch in patches:
                subprocess.run(['patch','-p1','--fuzz=0','-i',str(patch.resolve())],cwd=source,
                               check=True,capture_output=True,timeout=30)
            manifest=dict(schema=1,identity=key,archive=ARCHIVES[name],patches=pins,
                          files=tree_hashes(source))
            (staging/'manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
            # A concurrent preparer may publish the identical immutable cache.
            try:staging.rename(target)
            except OSError as error:
                if error.errno not in (errno.EEXIST,errno.ENOTEMPTY):raise
    if target.is_symlink():raise ValueError('Prepared source cache must not be a symlink')
    manifest=json.loads((target/'manifest.json').read_text())
    if manifest.get('identity')!=key or manifest.get('archive')!=ARCHIVES[name] or manifest.get('patches')!=pins:
        raise ValueError('Prepared source provenance mismatch')
    source=target/'source/tinyemu-2019-12-21'
    validate_tree(source,manifest['files'])
    return source

if __name__=='__main__':print(prepare_source())
