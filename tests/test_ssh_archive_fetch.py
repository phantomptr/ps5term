# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetching the owned server's dependencies must preserve legacy build state."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'ssh_prepare', Path(__file__).resolve().parents[1] / 'tools/prepare-ssh.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class ArchiveFetchTests(unittest.TestCase):
    def test_archive_only_preserves_extracted_sources_and_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / '.deps/ssh'
            (cache / 'downloads').mkdir(parents=True)
            (cache / 'wolfssh-test').mkdir()
            sentinel = cache / 'wolfssh-test' / 'keep'
            sentinel.write_text('active source')
            build = root / 'build/ssh'
            build.mkdir(parents=True)
            (build / 'keep').write_text('active build')
            archive = cache / 'downloads/wolfssh.tar.gz'
            archive.write_bytes(b'cached archive')
            expected = hashlib.sha256(archive.read_bytes()).hexdigest()
            old = prepare.ROOT, prepare.CACHE, prepare.DEPENDENCIES
            try:
                prepare.ROOT, prepare.CACHE = root, cache
                prepare.DEPENDENCIES = (('wolfssh', 'test', expected),)
                prepare.prepare(archives_only=True)
                self.assertEqual(sentinel.read_text(), 'active source')
                self.assertEqual((build / 'keep').read_text(), 'active build')
                prepare.DEPENDENCIES = (('wolfssh', 'test', '0' * 64),)
                with self.assertRaisesRegex(RuntimeError, 'SHA256 mismatch'):
                    prepare.prepare(archives_only=True)
                self.assertEqual((build / 'keep').read_text(), 'active build')
            finally:
                prepare.ROOT, prepare.CACHE, prepare.DEPENDENCIES = old


if __name__ == '__main__':
    unittest.main()
