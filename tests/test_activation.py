# PS5Term - guarded candidate activation regressions. GPL-3.0-or-later.
import base64
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.dev.activation import capture_installed, activate_candidate, read_tree, INSTALLED, remove_verified_transfer_holds
from tools.dev.ps5upload_client import ApiError
from tools.dev.hardware_cycle import candidate_manifest

class Console:
    addr = '192.168.86.99'
    def __init__(self):
        self.files = {'eboot.bin': b'known-good', 'sce_sys/param.json': b'{"titleId":"FAKE99260"}'}
        self.calls = []; self.running = True; self.fail_transfer = False; self.extra = False; self.held = False
    def request(self, method, path, body=None):
        import urllib.parse
        self.calls.append((method,path,body)); route=path.split('?')[0]
        if route == '/api/ps5/list-dir':
            q=urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)
            prefix=q['path'][0][len(INSTALLED):].strip('/')
            prefix=prefix+'/' if prefix else ''
            entries={}
            for name,data in self.files.items():
                if not name.startswith(prefix): continue
                rest=name[len(prefix):]; first=rest.split('/')[0]
                entries[first]={'name':first,'kind':'dir' if '/' in rest else 'file','size':0 if '/' in rest else len(data)}
            return {'entries':list(entries.values()),'truncated':False}
        if route == '/api/ps5/fs/read-preview':
            data=self.files[body['path'][len(INSTALLED)+1:]]
            data=data[body['offset']:body['offset']+body['max_bytes']]
            return {'base64':base64.b64encode(data).decode(),'size':len(data)}
        if route == '/api/ps5/process/list':
            return {'truncated':False,'processes':[{'pid':116,'title_id':'FAKE99260'}] if self.running else []}
        if route == '/api/ps5/process/kill':
            assert body['pid']==116; self.running=False; return {'ok':True}
        if route == '/api/transfer/dir':
            root=Path(body['src_dir'])
            self.files.update({p.relative_to(root).as_posix():p.read_bytes() for p in root.rglob('*') if p.is_file()})
            if self.held:
                self.files['sce_sys/param.json.ava-hold'] = self.files['sce_sys/param.json']
            if self.fail_transfer:
                self.fail_transfer=False
                raise ApiError('connection failed after partial write')
            return {'job_id':'job'}
        if route == '/api/jobs/job': return {'status':'done'}
        if route == '/api/ps5/fs/delete':
            del self.files[body['path'][len(INSTALLED)+1:]]; return {'ok':True}
        raise AssertionError(route)

class ActivationTests(unittest.TestCase):
    def candidate(self, root):
        root.mkdir(); (root/'sce_sys').mkdir(); (root/'eboot.bin').write_bytes(b'new-build')
        (root/'sce_sys/param.json').write_text('{"titleId":"FAKE99260"}')
        return root
    def test_backup_is_verified_before_stopping_app(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); result=capture_installed(c,Path(d)/'backup')
            self.assertEqual((Path(d)/'backup/eboot.bin').read_bytes(),b'known-good')
            self.assertEqual(len(result['files']),2)
            self.assertFalse(any('kill' in p for _,p,_ in c.calls))
    def test_candidate_activation_preserves_backup_and_removes_only_old_app_files(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); c.files['old.bin']=b'obsolete'
            candidate=self.candidate(Path(d)/'candidate')
            result=activate_candidate(candidate,c,Path(d)/'evidence')
            self.assertEqual(result['status'],'installed-awaiting-observed-launch')
            self.assertEqual(c.files['eboot.bin'],b'new-build'); self.assertNotIn('old.bin',c.files)
            self.assertEqual((Path(result['backup_dir'])/'eboot.bin').read_bytes(),b'known-good')
            self.assertFalse(any('launch' in p for _,p,_ in c.calls))
            self.assertTrue(all(not b or 'path' not in b or b['path'].startswith(INSTALLED+'/') for _,_,b in c.calls))
    def test_only_matching_transport_held_metadata_is_cleaned(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); c.held=True
            result=activate_candidate(self.candidate(Path(d)/'candidate'),c,Path(d)/'evidence')
            self.assertEqual(result['status'],'installed-awaiting-observed-launch')
            self.assertNotIn('sce_sys/param.json.ava-hold',c.files)
            self.assertEqual([b['path'] for _,p,b in c.calls if p.endswith('/delete')],
                             [INSTALLED+'/sce_sys/param.json.ava-hold'])

    def test_nonmatching_held_metadata_is_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); c.files['sce_sys/param.json.ava-hold']=b'unknown metadata'
            manifest=candidate_manifest(self.candidate(Path(d)/'candidate'))
            with self.assertRaises(ApiError): remove_verified_transfer_holds(c,manifest)
            self.assertEqual(c.files['sce_sys/param.json.ava-hold'],b'unknown metadata')
            self.assertFalse(any(p.endswith('/delete') for _,p,_ in c.calls))

    def test_transfer_failure_restores_known_good_without_launch(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); candidate=self.candidate(Path(d)/'candidate')
            (candidate/'new.bin').write_bytes(b'added')
            c.fail_transfer=True
            with self.assertRaises(ApiError): activate_candidate(candidate,c,Path(d)/'evidence')
            self.assertEqual(c.files['eboot.bin'],b'known-good')
            self.assertNotIn('new.bin',c.files)
            self.assertFalse(any('launch' in p for _,p,_ in c.calls))
    def test_backup_failure_never_stops_running_app(self):
        with tempfile.TemporaryDirectory() as d:
            c=Console(); c.files['bad-link']=b''
            original=c.request
            def request(method,path,body=None):
                r=original(method,path,body)
                if path.startswith('/api/ps5/list-dir'):
                    for x in r['entries']:
                        if x['name']=='bad-link': x['kind']='link'
                return r
            c.request=request
            with self.assertRaises(ValueError): activate_candidate(self.candidate(Path(d)/'candidate'),c,Path(d)/'evidence')
            self.assertTrue(c.running)
    def test_non_phat_fails_before_network(self):
        c=Console(); c.addr='192.168.86.100'
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError): capture_installed(c,Path(d)/'backup')
        self.assertEqual(c.calls,[])
if __name__=='__main__': unittest.main()
