"""Back up and activate a PS5Term candidate; never launch or touch runtime data.
GPL-3.0-or-later. Caller must hold hardware_cycle.TestWindow.
"""
from __future__ import annotations
import base64
import json
from pathlib import Path
import shutil
import time
import urllib.parse
import uuid
from .console_target import require_phat
from .hardware_cycle import candidate_manifest, safe_relative, poll_job, readback
from .ps5upload_client import ApiError

INSTALLED = '/data/homebrew/FAKE99260'

def read_tree(client, root=INSTALLED):
    require_phat(client.addr)
    files = {}; pending = ['']; total = 0; directories = 0
    while pending:
        directory = pending.pop(); directories += 1
        if directories > 512 or directory.count('/') > 32: raise ValueError('Directory bound exceeded')
        offset = 0
        while True:
            query = urllib.parse.urlencode(dict(path=root + ('/' + directory if directory else ''), offset=offset, limit=256))
            result = client.request('GET', '/api/ps5/list-dir?' + query)
            entries = result.get('entries')
            if not isinstance(entries, list) or len(entries) > 256: raise ApiError('Invalid listing')
            if result.get('truncated'): raise ApiError('Truncated listing; backup refused')
            for item in entries:
                name = item.get('name','')
                if name in ('.','..') or '/' in name: raise ValueError('Invalid directory entry')
                relative = safe_relative((directory + '/' if directory else '') + name)
                if item.get('kind') == 'dir': pending.append(relative)
                elif item.get('kind') == 'file':
                    size = item.get('size')
                    if type(size) is not int or size < 0: raise ValueError('Invalid file size')
                    total += size
                    if len(files) >= 4096 or total > 256 * 1024 * 1024: raise ValueError('Backup bound exceeded')
                    if relative in files: raise ApiError('Duplicate listing entry')
                    files[relative] = size
                else: raise ValueError('Nonregular app entry; refusing backup')
            offset += len(entries)
            if len(entries) < 256: break
            if offset >= 4096: raise ValueError('Listing bound exceeded')
    return files

def capture_installed(client, directory):
    require_phat(client.addr)
    files = read_tree(client)
    directory = Path(directory); directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    for name, size in files.items():
        target = directory / name; target.parent.mkdir(parents=True, exist_ok=True)
        target.touch(mode=0o600, exist_ok=False)
        offset = 0
        with target.open('wb') as out:
            while offset <= size:
                count = min(262144, size-offset+1)
                result = client.request('POST', '/api/ps5/fs/read-preview',
                    dict(path=INSTALLED+'/'+name, offset=offset, max_bytes=count))
                data = base64.b64decode(result['base64'], validate=True)
                if len(data) > count or result.get('size') != len(data): raise ApiError('Backup read size mismatch')
                if not data: break
                if offset + len(data) > size: raise ApiError('App changed during backup')
                out.write(data); offset += len(data)
        if offset != size: raise ApiError('Incomplete backup')
    if files != read_tree(client): raise ApiError('App tree changed during backup')
    manifest = candidate_manifest(directory)
    for name, entry in manifest['files'].items(): readback(client, INSTALLED+'/'+name, entry)
    return manifest

def app_processes(client):
    result = client.request('GET','/api/ps5/process/list')
    if result.get('truncated'): raise ApiError('Truncated process list')
    return [p for p in result.get('processes',[]) if p.get('title_id') == 'FAKE99260']

def stop_app(client):
    processes = app_processes(client)
    if len(processes) > 1: raise ApiError('Ambiguous title ownership')
    if not processes: return
    pid = processes[0].get('pid')
    if type(pid) is not int or pid <= 1: raise ApiError('Invalid app PID')
    # Recheck title identity immediately before the narrow PID operation.
    refreshed = app_processes(client)
    if len(refreshed) != 1 or refreshed[0].get('pid') != pid:
        raise ApiError('App identity changed before stop')
    client.request('POST','/api/ps5/process/kill',dict(pid=pid))
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        if not app_processes(client): return
        time.sleep(.2)
    raise TimeoutError('App has not exited; activation refused')

def remove_verified_transfer_holds(client, manifest):
    # ps5upload holds registration metadata until transfer completion. Some helper
    # versions leave an identical duplicate on overwrite. Never remove arbitrary
    # leftovers, or a held copy that is the only correct registration metadata.
    current = read_tree(client)
    for name in ('sce_sys/param.json', 'sce_sys/param.sfo'):
        held = name + '.ava-hold'
        if name not in manifest['files'] or held in manifest['files'] or held not in current:
            continue
        entry = manifest['files'][name]
        readback(client, INSTALLED + '/' + name, entry)
        readback(client, INSTALLED + '/' + held, entry)
        client.request('POST', '/api/ps5/fs/delete', dict(path=INSTALLED + '/' + held))


def transfer_verified(root, manifest, client):
    if app_processes(client): raise ApiError('App appeared before transfer')
    job=client.request('POST','/api/transfer/dir',dict(src_dir=str(Path(root).resolve()),dest_root=INSTALLED))
    poll_job(client,job.get('job_id'))
    for name,entry in manifest['files'].items(): readback(client,INSTALLED+'/'+name,entry)
    remove_verified_transfer_holds(client, manifest)

def activate_candidate(candidate, client, evidence):
    require_phat(client.addr)
    candidate=Path(candidate); manifest=candidate_manifest(candidate)
    run=Path(evidence)/uuid.uuid4().hex; run.mkdir(mode=0o700,parents=True)
    snapshot=run/'candidate'; shutil.copytree(candidate,snapshot)
    if candidate_manifest(snapshot)!=manifest: raise ApiError('Candidate changed during snapshot')
    result=dict(schema=1,addr=client.addr,status='backing-up',backup_dir=str((run/'known-good').resolve()),manifest=manifest)
    try:
        backup=capture_installed(client,run/'known-good'); result['backup_manifest']=backup
        result['status']='stopping'; stop_app(client)
        try:
            result['status']='installing'; transfer_verified(snapshot,manifest,client)
            current=read_tree(client)
            # Never recursively delete a former file that has become a directory.
            for name in sorted(set(backup['files'])-set(manifest['files'])):
                if name not in current: continue
                client.request('POST','/api/ps5/fs/delete',dict(path=INSTALLED+'/'+name))
            expected={name:entry['size'] for name,entry in manifest['files'].items()}
            if read_tree(client)!=expected: raise ApiError('Installed file list mismatch')
            result['status']='installed-awaiting-observed-launch'
        except Exception:
            result['status']='activation-failed'
            try:
                transfer_verified(run/'known-good',backup,client)
                current=read_tree(client)
                for name in sorted(set(manifest['files'])-set(backup['files'])):
                    if name in current:
                        client.request('POST','/api/ps5/fs/delete',dict(path=INSTALLED+'/'+name))
                expected={name:entry['size'] for name,entry in backup['files'].items()}
                if read_tree(client)!=expected: raise ApiError('Rollback file list mismatch')
                result['status']='known-good-restored-awaiting-observed-launch'
            except Exception as error:
                result['rollback_failure']=type(error).__name__
            raise
        return result
    finally:
        record=run/'activation.json'; record.write_text(json.dumps(result,indent=2)+'\n'); record.chmod(0o600)
