# PS5Term development harness regressions. GPL-3.0-or-later.
import base64
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.dev.console_target import require_phat
from tools.dev.ps5upload_client import Ps5UploadClient, ApiError
from tools.dev.hardware_cycle import (deploy_candidate, cleanup_obsolete, TestWindow,
                                      verify_readiness, launch_candidate)


class FakeClient:
    addr = '192.168.86.99'
    def __init__(self, root, failure=None):
        self.root, self.failure, self.calls = root, failure, []
    def request(self, method, path, body=None):
        self.calls.append((method, path, body))
        if path == '/api/ps5/hw/info':
            return {'model': 'CFI-1015A', 'sdk_version': '13.60', 'firmware_version': '13.60'}
        if path == '/api/transfer/dir': return {'job_id': 'test-job'}
        if path == '/api/jobs/test-job':
            if self.failure == 'timeout': return {'status': 'running'}
            return {'status': 'failed' if self.failure == 'job' else 'done'}
        if path == '/api/ps5/fs/read-preview':
            data = (self.root / body['path'].split('/FAKE99260/', 1)[1].split('/', 1)[1]).read_bytes()
            if self.failure == 'hash': data = b'bad' + data[3:]
            data = data[body['offset']:body['offset'] + body['max_bytes']]
            return {'base64': base64.b64encode(data).decode(), 'size': len(data)}
        if path == '/api/ps5/fs/delete': return {'ok': True}
        raise AssertionError(path)


class HardwareCycleTests(unittest.TestCase):
    def candidate(self, directory):
        root = Path(directory) / 'FAKE99260'
        (root / 'sce_sys').mkdir(parents=True)
        (root / 'sce_sys/param.json').write_text(json.dumps({'titleId': 'FAKE99260'}))
        (root / 'eboot.bin').write_bytes(b'candidate-not-known-good')
        return root

    def test_rejects_pro_before_network(self):
        with patch('urllib.request.urlopen') as network:
            with self.assertRaises(ValueError): Ps5UploadClient('192.168.86.100')
            network.assert_not_called()

    def test_rejects_missing_target(self):
        for value in ('', None, 'phat', '192.168.86.99:9021', '192.168.86.99 '):
            with self.assertRaises(ValueError): require_phat(value)

    def test_client_rejects_target_override_and_generic_launch(self):
        client = Ps5UploadClient('192.168.86.99')
        with patch('urllib.request.urlopen') as network:
            for path, body in [('/api/ps5/process/list?addr=192.168.86.100', None),
                               ('/api/ps5/hw/info', {'addr': '192.168.86.100'}),
                               ('/api/ps5/app/launch', {'title_id': 'FAKE99260'}),
                               ('/api/ps5/elfldr/ensure', {})]:
                with self.assertRaises((ValueError, ApiError)): client.request('POST', path, body)
            network.assert_not_called()

    def test_client_limits_diagnostic_writes_and_process_stop(self):
        client = Ps5UploadClient('192.168.86.99')
        with patch('urllib.request.build_opener') as network:
            for body in ({'path': '/data/ps5term/ssh/key', 'bytes_b64': 'eA=='},
                         {'path': '/data/ps5term/probe.request', 'bytes_b64': 'eA=='},
                         {'path': '/data/ps5term/diagnostics.enabled', 'bytes_b64': 'eA==', 'create_only': True}):
                with self.assertRaises(ApiError): client.request('POST', '/api/ps5/fs/write-bytes', body)
            with self.assertRaises(ApiError): client.request('POST', '/api/ps5/process/kill', {'pid': 1})
            for body in ({'path': '/data/ps5term/ssh/key', 'mode': '0644'},
                         {'path': '/data/ps5term/status.json', 'mode': '0777'}):
                with self.assertRaises(ApiError): client.request('POST', '/api/ps5/fs/chmod', body)
            network.assert_not_called()
        with patch.object(client, 'request', wraps=client.request) as request, patch('urllib.request.build_opener') as network:
            def dispatch(method, path, body=None):
                if path == '/api/ps5/process/list': return {'processes': [{'pid': 50, 'title_id': 'OTHER'}]}
                return Ps5UploadClient.request(client, method, path, body)
            request.side_effect = dispatch
            with self.assertRaises(ApiError): client.request('POST', '/api/ps5/process/kill', {'pid': 50})
            network.assert_not_called()

    def test_job_failure_prevents_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.candidate(tmp); client = FakeClient(root, 'job')
            with self.assertRaises(ApiError): deploy_candidate(root, client, Path(tmp) / 'evidence')
            self.assertFalse(any('launch' in p for _, p, _ in client.calls))
            self.assertFalse(any('delete' in p or 'move' in p for _, p, _ in client.calls))

    def test_readback_hash_mismatch_prevents_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.candidate(tmp); client = FakeClient(root, 'hash')
            with self.assertRaises(ApiError): deploy_candidate(root, client, Path(tmp) / 'evidence')
            self.assertFalse(any('launch' in p for _, p, _ in client.calls))

    def test_candidate_success_only_stages_and_records_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.candidate(tmp); client = FakeClient(root)
            result = deploy_candidate(root, client, Path(tmp) / 'evidence')
            self.assertEqual(result['status'], 'staged-verified')
            self.assertIn('/data/ps5term-dev/', result['remote_root'])
            self.assertEqual(len(result['manifest']['files']), 2)
            self.assertFalse(any(p.endswith(('kill', 'move', 'launch', 'delete')) for _, p, _ in client.calls))

    def test_obsolete_cleanup_is_manifest_scoped(self):
        client = FakeClient(Path('/unused'))
        prior = {'files': {'old.bin': {}, 'eboot.bin': {}}}
        current = {'files': {'eboot.bin': {}}}
        cleanup_obsolete(client, '/data/ps5term-dev/staging/FAKE99260/test', prior, current)
        self.assertEqual([b['path'] for _, _, b in client.calls],
                         ['/data/ps5term-dev/staging/FAKE99260/test/old.bin'])
        for name in ('../ssh/key', '/data/ps5term/ssh', 'dir/../eboot.bin', 'dir\\file'):
            with self.assertRaises(ValueError):
                cleanup_obsolete(client, '/data/ps5term-dev/staging/FAKE99260/test', {'files': {name: {}}}, current)
        with self.assertRaises(ValueError):
            cleanup_obsolete(client, '/data/ps5term', prior, current)

    def test_timeout_preserves_known_good(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.candidate(tmp); client = FakeClient(root, 'timeout')
            with self.assertRaises(TimeoutError):
                deploy_candidate(root, client, Path(tmp) / 'evidence', timeout=0)
            self.assertFalse(any(p.endswith(('kill', 'delete', 'move', 'launch')) for _, p, _ in client.calls))
            self.assertEqual((root / 'eboot.bin').read_bytes(), b'candidate-not-known-good')

    def test_symlink_candidate_rejected_before_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.candidate(tmp); (root / 'secret').symlink_to('/etc/passwd')
            client = FakeClient(root)
            with self.assertRaises(ValueError): deploy_candidate(root, client, Path(tmp) / 'evidence')
            self.assertEqual(client.calls, [])

    def test_exclusive_window(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock = Path(tmp) / 'console.lock'
            with TestWindow(lock):
                with self.assertRaises(RuntimeError):
                    with TestWindow(lock): pass
            with TestWindow(lock): pass

    def state(self, **updates):
        value = dict(schema=1, build_id='new-build', instance_id='new-instance',
                     pid=123, title_id='FAKE99260', foreground=True)
        value.update(updates)
        return value

    def test_launch_http_success_is_not_readiness(self):
        self.assertFalse(verify_readiness({'ok': True}, 'new-build', 'old-instance'))
    def test_wrong_build_rejected(self):
        self.assertFalse(verify_readiness(self.state(build_id='old'), 'new-build', 'old-instance'))
    def test_old_instance_rejected(self):
        self.assertFalse(verify_readiness(self.state(instance_id='old-instance'), 'new-build', 'old-instance'))
    def test_no_focus_not_passed(self):
        self.assertFalse(verify_readiness(self.state(foreground=False), 'new-build', 'old-instance'))
    def test_launch_timeout_does_not_retry_or_touch_shellui(self):
        client = FakeClient(Path('/unused'))
        with self.assertRaises(ApiError): launch_candidate('engine', client, 'new-build')
        with self.assertRaises(ApiError): launch_candidate('remoteplay-ui', client, 'new-build')
        self.assertEqual(client.calls, [])
    def test_manual_launch_is_reported_manual(self):
        result = launch_candidate('manual', FakeClient(Path('/unused')), 'new-build')
        self.assertEqual(result['status'], 'awaiting-manual-launch')
        self.assertFalse(result['automated'])

if __name__ == '__main__': unittest.main()
