# PS5Term distribution profile regression. GPL-3.0-or-later.
import importlib.util
import json
from pathlib import Path
import tempfile
import shutil
import subprocess
import zipfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class AppDistributionTests(unittest.TestCase):
    def load(self):
        spec=importlib.util.spec_from_file_location('app_distribution',ROOT/'tools/app_distribution.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
    def fixture(self,root,definitions):
        app=root/'dist/FAKE99260';(app/'assets').mkdir(parents=True)
        (app/'assets/build-info.json').write_text(json.dumps(dict(schema=1,definitions=definitions)))
        assets=root/'assets/backend';assets.mkdir(parents=True)
        for name in ('ps5sshd.elf','ps5term-sh.elf','keep.txt'):(assets/name).write_bytes(b'fixture')
        return app
    def test_owned_profile_stages_no_unused_payloads(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);app=self.fixture(root,['PS5TERM_OWNED_SSH','PS5TERM_OWNED_SFTP'])
            module.stage_assets(root,app)
            self.assertEqual({p.name for p in (app/'assets/backend').iterdir()},{'keep.txt'})
            self.assertEqual(module.profile(app)[1],'OWNED_SSH.md')
    def test_legacy_and_explicit_shell_research_keep_required_payloads(self):
        module=self.load()
        for definitions,names,readme in [([],{'ps5sshd.elf','ps5term-sh.elf','keep.txt'},'SSH.md'),
            (['PS5TERM_OWNED_SSH','PS5TERM_LEGACY_SHELL_RESEARCH'],{'ps5term-sh.elf','keep.txt'},'OWNED_SSH.md')]:
            with self.subTest(definitions=definitions),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);app=self.fixture(root,definitions);module.stage_assets(root,app)
                self.assertEqual({p.name for p in (app/'assets/backend').iterdir()},names)
                self.assertEqual(module.profile(app)[1],readme)
    def test_linux_images_are_opt_in_and_pinned(self):
        import hashlib
        module=self.load()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); app=self.fixture(root,['PS5TERM_OWNED_SSH'])
            images=root/'assets/linux';images.mkdir()
            data=b'linux-fixture';(images/'bbl64.bin').write_bytes(data)
            pins=root/'tooling/subsystem';pins.mkdir(parents=True)
            (pins/'guest-images.json').write_text(json.dumps(dict(images={'bbl64.bin':dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())})))
            module.stage_assets(root,app)
            self.assertFalse((app/'assets/linux').exists())
            (app/'assets/build-info.json').write_text(json.dumps(dict(schema=1,definitions=['PS5TERM_OWNED_SSH','PS5TERM_LINUX_SUBSYSTEM'])))
            module.stage_assets(root,app)
            self.assertEqual((app/'assets/linux/bbl64.bin').read_bytes(),data)
            (images/'bbl64.bin').write_bytes(b'wrong-fixture')
            with self.assertRaises(ValueError):module.stage_assets(root,app)

    def test_persistence_requires_pinned_seed(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            app=self.fixture(root,['PS5TERM_LINUX_SUBSYSTEM','PS5TERM_LINUX_PERSISTENCE'])
            images=root/'assets/linux';images.mkdir()
            (root/'tooling/subsystem').mkdir(parents=True)
            (root/'tooling/subsystem/guest-images.json').write_text(json.dumps(dict(images={})))
            with self.assertRaisesRegex(ValueError,'seed'):
                module.stage_assets(root,app)

    def test_network_requires_network_pins_and_notices(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            app=self.fixture(root,['PS5TERM_LINUX_SUBSYSTEM','PS5TERM_LINUX_NETWORK'])
            images=root/'assets/linux';images.mkdir()
            (root/'tooling/subsystem').mkdir(parents=True)
            pins=root/'tooling/subsystem/guest-images.json'
            pins.write_text(json.dumps(dict(network=False,images={})))
            with self.assertRaisesRegex(ValueError,'network-capable'):
                module.stage_assets(root,app)
            pins.write_text(json.dumps(dict(network=True,images={})))
            with self.assertRaisesRegex(ValueError,'libslirp notice'):
                module.stage_assets(root,app)
            notices=images/'licenses/libslirp-4.9.7';notices.mkdir(parents=True)
            for name in ('LICENSE','COPYRIGHT'):(notices/name).write_text(name)
            module.stage_assets(root,app)
            self.assertTrue((app/'assets/linux/licenses/libslirp-4.9.7/LICENSE').is_file())

    def test_invalid_profile_is_refused(self):
        module=self.load()
        for definitions in (['PS5TERM_OWNED_SFTP'],['PS5TERM_LINUX_PERSISTENCE'],['PS5TERM_LINUX_NETWORK'],'PS5TERM_OWNED_SSH',[123]):
            with self.subTest(definitions=definitions),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);app=self.fixture(root,definitions)
                with self.assertRaises(ValueError):module.profile(app)

    def test_package_matches_owned_profile_and_rejects_stale_payloads(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);app=self.fixture(root,['PS5TERM_OWNED_SSH','PS5TERM_OWNED_SFTP'])
            module.stage_assets(root,app)
            (root/'tools').mkdir();(root/'docs').mkdir();(root/'sce_sys').mkdir()
            (root/'sce_sys/param.json').write_text(json.dumps(dict(titleId='FAKE99260',contentVersion='00.001.000')))
            for name in ('package-app.py','app_distribution.py'):shutil.copy2(ROOT/'tools'/name,root/'tools'/name)
            for name in ('SSH.md','OWNED_SSH.md','TERMINAL_CLIENTS.md','SSH-SOURCES.md','FEATURES.md','SUBSYSTEM.md'):(root/'docs'/name).write_text(name)
            for name in ('LICENSE','NOTICE.md'):(root/name).write_text(name)
            for name in ('eboot.bin','sce_sys/param.json','sce_sys/icon0.png','sce_module/libc.prx'):
                path=app/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            script=['python3',str(root/'tools/package-app.py')]
            result=subprocess.run(script,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            with zipfile.ZipFile(root/'dist/PS5Term-00.001.000.zip') as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.read('FAKE99260/README.md'),b'OWNED_SSH.md')
                self.assertFalse(any(name.endswith('.elf') for name in archive.namelist()))
            stale=app/'assets/backend/ps5sshd.elf';stale.write_bytes(b'legacy')
            result=subprocess.run(script,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('Unused payload',result.stderr)
