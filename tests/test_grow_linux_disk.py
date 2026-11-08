# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    'grow_linux_disk', Path(__file__).resolve().parents[1] / 'tools/dev/grow_linux_disk.py')
mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)


class GrowDiskTests(unittest.TestCase):
    def test_resize_preserves_original_and_contents(self):
        try:
            mkfs = mod.find_tool('mke2fs')
            debugfs = mod.find_tool('debugfs')
        except FileNotFoundError as error:
            self.skipTest(str(error))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / 'original.ext2', root / 'larger.ext2'
            with source.open('wb') as disk:
                disk.truncate(16 * 1024 * 1024)
            subprocess.run([mkfs, '-q', '-t', 'ext2', '-F', str(source)], check=True)
            (root / 'note').write_text('keep my files\n')
            subprocess.run([debugfs, '-w', '-R', f'write {root / "note"} /note', str(source)],
                           check=True, capture_output=True)
            before = hashlib.sha256(source.read_bytes()).digest()
            mod.grow(source, output, 32)
            self.assertEqual(output.stat().st_size, 32 * 1024 * 1024)
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
            content = subprocess.run([debugfs, '-R', 'cat /note', str(output)],
                                     check=True, capture_output=True).stdout
            self.assertEqual(content, b'keep my files\n')
            with self.assertRaises(FileExistsError):
                mod.grow(source, output, 64)
            with self.assertRaises(ValueError):
                mod.grow(source, root / 'smaller', 8)
            with mock.patch.object(mod, 'run', side_effect=RuntimeError('resize failed')):
                with self.assertRaises(RuntimeError):
                    mod.grow(source, root / 'tool-failed', 32)
            self.assertFalse((root / 'tool-failed').exists())
            self.assertFalse(list(root.glob('.ps5term-grow-*')))
            with source.open('rb') as locked:
                mod.fcntl.flock(locked, mod.fcntl.LOCK_EX | mod.fcntl.LOCK_NB)
                with self.assertRaises(BlockingIOError):
                    mod.grow(source, root / 'locked', 32)
            self.assertFalse((root / 'locked').exists())
            damaged = root / 'damaged'
            shutil.copyfile(source, damaged)
            with damaged.open('r+b') as disk:
                disk.seek(1080)
                disk.write(b'XX')
            with self.assertRaises(ValueError):
                mod.grow(damaged, root / 'failed', 32)
            self.assertFalse((root / 'failed').exists())
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
