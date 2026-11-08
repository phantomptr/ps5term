# SPDX-License-Identifier: GPL-3.0-or-later
"""Port preparation rejects corrupt sources before changing an existing tree."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


class BashPortTests(unittest.TestCase):
    def load(self):
        path = Path(__file__).resolve().parents[1] / 'tools/prepare-bash.py'
        spec = importlib.util.spec_from_file_location('bash_port', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_corrupt_archive_preserves_existing_source(self):
        port = self.load()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / 'downloads'
            cache.mkdir()
            (cache / 'source.tar.gz').write_bytes(b'corrupt')
            source = root / 'source'
            source.mkdir()
            (source / 'keep').write_text('active work')
            records = [{'url': 'https://ftp.gnu.org/gnu/bash/source.tar.gz',
                        'sha256': hashlib.sha256(b'expected').hexdigest()}]
            with self.assertRaisesRegex(RuntimeError, 'SHA256 mismatch'):
                port.prepare(records, cache, source)
            self.assertEqual((source / 'keep').read_text(), 'active work')

    def test_existing_source_is_preserved_even_with_verified_cache(self):
        port = self.load()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / 'downloads'
            cache.mkdir()
            (cache / 'source.tar.gz').write_bytes(b'verified')
            source = root / 'source'
            source.mkdir()
            (source / 'keep').write_text('active work')
            records = [{'url': 'https://ftp.gnu.org/gnu/bash/source.tar.gz',
                        'sha256': hashlib.sha256(b'verified').hexdigest()}]
            with self.assertRaisesRegex(RuntimeError, 'already exists'):
                port.prepare(records, cache, source)
            self.assertEqual((source / 'keep').read_text(), 'active work')


if __name__ == '__main__':
    unittest.main()
