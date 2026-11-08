"""Guarded phat candidate staging. No generic launch, reboot or loader mutation.
Run as: python3 -m tools.dev.hardware_cycle --addr 192.168.86.99 --app-dir dist/FAKE99260
SPDX-License-Identifier: GPL-3.0-or-later
"""
from __future__ import annotations
import argparse
import base64
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import time
import uuid
from .console_target import require_phat
from .ps5upload_client import ApiError, Ps5UploadClient

TITLE_ID = 'FAKE99260'
STAGING = '/data/ps5term-dev/staging/FAKE99260/'


class TestWindow:
    """Advisory lock: other tools must use this same path to coordinate."""
    def __init__(self, path): self.path = Path(path)
    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)
        try: fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            os.close(self.fd)
            raise RuntimeError('Console test window is already held') from None
        os.ftruncate(self.fd, 0)
        os.write(self.fd, f'{os.getpid()}\n'.encode())
        return self
    def __exit__(self, *_):
        fcntl.flock(self.fd, fcntl.LOCK_UN)
        os.close(self.fd)


def safe_relative(value):
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or '\\' in value or '\0' in value or
            any(part in ('', '.', '..') for part in value.split('/'))):
        raise ValueError('Unsafe manifest path')
    return value


def candidate_manifest(root):
    root = Path(root)
    if root.is_symlink() or not root.is_dir(): raise ValueError('App directory required')
    files = {}
    total = 0
    for path in sorted(root.rglob('*')):
        if path.is_symlink(): raise ValueError('Candidate symlinks are refused')
        if path.is_dir(): continue
        if not path.is_file(): raise ValueError('Candidate must contain ordinary files')
        relative = safe_relative(path.relative_to(root).as_posix())
        data = path.read_bytes(); total += len(data)
        if len(files) >= 4096 or total > 256 * 1024 * 1024:
            raise ValueError('Candidate exceeds development staging bounds')
        files[relative] = dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())
    if 'eboot.bin' not in files: raise ValueError('Candidate has no eboot.bin')
    param = json.loads((root / 'sce_sys/param.json').read_text())
    if param.get('titleId') != TITLE_ID: raise ValueError('Wrong candidate title')
    identity = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return dict(schema=1, title_id=TITLE_ID, artifact_id=identity, files=files)


def poll_job(client, job_id, timeout=120):
    if not isinstance(job_id, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', job_id):
        raise ApiError('Missing or invalid transfer job identity')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = client.request('GET', '/api/jobs/' + job_id)
        state = result.get('status')
        if state == 'done': return result
        if state in ('failed', 'cancelled', 'canceled'): raise ApiError('Candidate transfer failed')
        time.sleep(.25)
    raise TimeoutError('Transfer did not finish; installed app was not changed')


def readback(client, remote, expected):
    digest = hashlib.sha256(); offset = 0
    # Also request one byte beyond expected EOF to catch trailing/stale file bytes.
    while offset <= expected['size']:
        count = min(262144, expected['size'] - offset + 1)
        answer = client.request('POST', '/api/ps5/fs/read-preview',
                                dict(path=remote, offset=offset, max_bytes=count))
        data = base64.b64decode(answer['base64'], validate=True)
        if len(data) > count or answer.get('size') != len(data):
            raise ApiError('Readback length mismatch')
        if not data: break
        digest.update(data); offset += len(data)
    if offset != expected['size'] or digest.hexdigest() != expected['sha256']:
        raise ApiError('Candidate readback hash mismatch')


def cleanup_obsolete(client, remote_root, prior, current):
    require_phat(client.addr)
    if not remote_root.startswith(STAGING) or not re.fullmatch(r'[A-Za-z0-9_-]+', remote_root[len(STAGING):]):
        raise ValueError('Cleanup is restricted to one development staging directory')
    # Validate every path before performing the first mutation.
    previous = {safe_relative(name) for name in prior['files']}
    retained = {safe_relative(name) for name in current['files']}
    obsolete = sorted(previous - retained)
    # Refuse a prior file turning into a parent directory of a current file: the
    # engine delete is recursive. Never remove that directory's new contents.
    if any(other.startswith(name + '/') for name in obsolete for other in retained):
        raise ValueError('File/directory conflict requires a fresh staging directory')
    for name in obsolete:
        client.request('POST', '/api/ps5/fs/delete', dict(path=remote_root + '/' + name))
    return obsolete


def deploy_candidate(app_dir: Path, client: Ps5UploadClient, evidence_dir: Path, timeout=120) -> dict:
    require_phat(client.addr)
    manifest = candidate_manifest(app_dir)  # Fail before network on invalid input.
    evidence_dir = Path(evidence_dir)
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence_dir.chmod(0o700)
    run_id = uuid.uuid4().hex
    run = evidence_dir / run_id; run.mkdir(mode=0o700)
    snapshot = run / 'candidate'; shutil.copytree(app_dir, snapshot)
    if candidate_manifest(snapshot) != manifest: raise ValueError('Candidate changed during snapshot')
    remote = STAGING + run_id
    result = dict(schema=1, addr=client.addr, status='preflight', manifest=manifest,
                  remote_root=remote, automated_launch=False)
    record = run / 'result.json'
    try:
        # Read-only helper availability. Model/FW are captured, not guessed from IP.
        info = client.request('GET', '/api/ps5/hw/info')
        (run / 'hardware.json').write_text(json.dumps(info, indent=2) + '\n')
        result['status'] = 'transferring'
        job = client.request('POST', '/api/transfer/dir',
                             dict(src_dir=str(snapshot.resolve()), dest_root=remote))
        result['job_id'] = job.get('job_id')
        poll_job(client, result['job_id'], timeout)
        for name, entry in manifest['files'].items(): readback(client, remote + '/' + name, entry)
        result['status'] = 'staged-verified'
        return result
    except Exception as error:
        result['status'] = 'failed'; result['failure_type'] = type(error).__name__
        raise
    finally:
        record.write_text(json.dumps(result, indent=2) + '\n'); record.chmod(0o600)


def verify_readiness(state, expected_build, previous_instance):
    return (state.get('schema') == 1 and state.get('build_id') == expected_build and
            isinstance(state.get('instance_id'), str) and bool(state['instance_id']) and
            state['instance_id'] != previous_instance and type(state.get('pid')) is int and
            state['pid'] > 0 and state.get('title_id') == TITLE_ID and
            state.get('foreground') is True)


def launch_candidate(strategy: str, client: Ps5UploadClient, expected_build: str) -> dict:
    require_phat(client.addr)
    if strategy == 'manual':
        return dict(status='awaiting-manual-launch', automated=False, expected_build=expected_build)
    raise ApiError('Automated relaunch gated on observed Remote Play UI and instance/focus checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--addr', required=True)
    parser.add_argument('--app-dir', type=Path, required=True)
    parser.add_argument('--engine', default='http://127.0.0.1:19113')
    args = parser.parse_args()
    client = Ps5UploadClient(args.addr, args.engine)
    root = Path(__file__).resolve().parents[2] / '.local/dev'
    with TestWindow(root / 'console.lock'):
        result = deploy_candidate(args.app_dir, client, root / 'evidence')
    print(json.dumps(dict(status=result['status'], artifact_id=result['manifest']['artifact_id'])))

if __name__ == '__main__': main()
