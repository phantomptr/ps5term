# GPL-3.0-or-later.
import base64
import unittest
from tools.dev.app_diagnostics import capture_readiness, capture_capabilities, capture_child_experiment, read_report
from tools.dev.ps5upload_client import ApiError
import json

class Client:
    addr = '192.168.86.99'
    def __init__(self):
        self.status = dict(schema=1, title_id='FAKE99260', build_id='build', instance_id='new', pid=20)
        self.app = dict(pid=20, title_id='FAKE99260', app_id=42)
        self.focus = 42
        self.capabilities = dict(schema=1, context='app', scope='baseline-only', build_id='build',
                                 cleanup_ok=True, results=[dict(pid=20) for _ in range(7)])
        self.child = dict(self.capabilities, scope='child-experiment', complete=True, child_pid=21)
        self.calls = []
    def request(self, method, path, body=None):
        self.calls.append((method, path, body))
        if path.endswith('chmod'): return {'ok': True}
        if path.endswith('read-preview'):
            report = self.child if body['path'].endswith('child-capabilities.json') else self.capabilities
            data=json.dumps(self.status if body['path'].endswith('status.json') else report).encode()
            return dict(base64=base64.b64encode(data).decode(), size=len(data))
        if path.endswith('process/list'): return dict(processes=[self.app])
        if path.endswith('focus'): return dict(focus_app_id=self.focus)
        raise AssertionError(path)

class DiagnosticsTests(unittest.TestCase):
    def test_readiness_requires_independent_process_and_focus(self):
        client=Client()
        result=capture_readiness(client,'build','old')
        self.assertTrue(result['readiness_verified'])
        client.app['pid']=30
        with self.assertRaises(ApiError): capture_readiness(client,'build','old')
        client.app['pid']=20; client.focus=99
        with self.assertRaises(ApiError): capture_readiness(client,'build','old')
    def test_stale_instance_or_build_refused(self):
        for build,instance in [('wrong','old'),('build','new')]:
            with self.assertRaises(ApiError): capture_readiness(Client(),build,instance)
    def test_capabilities_must_belong_to_current_app(self):
        client=Client(); ready=capture_readiness(client,'build')
        self.assertEqual(len(capture_capabilities(client,ready)['results']),7)
        client.capabilities['results'][0]['pid']=19
        with self.assertRaises(ApiError): capture_capabilities(client,ready)
    def test_report_access_never_touches_credentials(self):
        client=Client()
        with self.assertRaises(ValueError): read_report(client,'ssh/key')
        self.assertEqual(client.calls,[])

    def test_child_report_preserves_failure_and_rejects_stale_context(self):
        client=Client(); ready=capture_readiness(client,'build')
        self.assertTrue(capture_child_experiment(client,ready)['complete'])
        client.child.update(complete=False, cleanup_ok=False)
        self.assertFalse(capture_child_experiment(client,ready)['complete'])
        client.child['build_id']='old'
        with self.assertRaises(ApiError): capture_child_experiment(client,ready)
