# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-only subsystem research tooling; never a PS5 compatibility assertion."""
import io
from pathlib import Path
import tempfile
import tarfile
import unittest
import contextlib
import json
from tools.dev.embedded_linux import extract_checked, guest_config, verify_archive

class EmbeddedLinuxTests(unittest.TestCase):
    def test_console_markers_accept_repeated_cr_without_accepting_echo(self):
        from tools.dev.embedded_linux_smoke import console_markers
        self.assertEqual(console_markers(b'PS5TERM_LINUX\r\r\nPS5TERM_DONE\r\n'), {'LINUX', 'DONE'})
        self.assertEqual(console_markers(b"printf 'PS5TERM_%s\\n' DONE\r\n"), set())

    def test_deadline_reaps_owned_guest_and_preserves_image(self):
        from tools.dev.embedded_linux_smoke import run
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            binary=root/'source/tinyemu-2019-12-21/temu'
            binary.parent.mkdir(parents=True)
            binary.write_text('#!/bin/sh\nsleep 60\n')
            binary.chmod(0o700)
            images=root/'images/diskimage-linux-riscv-2018-09-23'
            images.mkdir(parents=True)
            (images/'root-riscv64.bin').write_bytes(b'fixture')
            (root/'guest.cfg').write_text(json.dumps(guest_config(images)))
            evidence=root/'result.json'
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(run(root,evidence,deadline_seconds=1))
            report=json.loads(evidence.read_text())
            self.assertTrue(report['guest_process_reaped'])
            self.assertTrue(report['image_unchanged'])
            self.assertFalse(report['completed'])
            self.assertLess(report['elapsed_seconds'],5)

    def archive(self, root, name='source/file.c', kind=tarfile.REGTYPE):
        path=root/'source.tar.gz'
        with tarfile.open(path,'w:gz') as archive:
            entry=tarfile.TarInfo(name);entry.type=kind;entry.size=1 if kind==tarfile.REGTYPE else 0
            entry.linkname='../../outside'
            archive.addfile(entry,io.BytesIO(b'x') if entry.size else None)
        return path
    def test_rejects_traversal_and_links(self):
        for name,kind in [('../escape',tarfile.REGTYPE),('/absolute',tarfile.REGTYPE),('source/link',tarfile.SYMTYPE),('source/link',tarfile.LNKTYPE)]:
            with self.subTest(name=name,kind=kind), tempfile.TemporaryDirectory() as temp:
                root=Path(temp);archive=self.archive(root,name,kind)
                with self.assertRaises(ValueError): extract_checked(archive,root/'out')
                self.assertFalse((root/'out').exists())
    def test_expansion_budget_checked_before_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=self.archive(root)
            with self.assertRaises(ValueError): extract_checked(archive,root/'out',max_bytes=0)
            self.assertFalse((root/'out').exists())
    def test_extracts_regular_files_and_refuses_existing_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive=self.archive(root)
            extract_checked(archive,root/'out')
            self.assertEqual((root/'out/source/file.c').read_bytes(),b'x')
            with self.assertRaises(FileExistsError): extract_checked(archive,root/'out')
    def test_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.archive(Path(temp))
            with self.assertRaises(ValueError): verify_archive(path,'0'*64)
    def test_configuration_has_no_network_or_host_share(self):
        config=guest_config(Path('/tmp/local-images'))
        self.assertEqual(config['machine'],'riscv64')
        self.assertEqual(config['memory_size'],128)
        self.assertEqual(set(config),{'version','machine','memory_size','bios','kernel','cmdline','drive0'})
        self.assertNotIn('http',str(config))

if __name__=='__main__':unittest.main()
