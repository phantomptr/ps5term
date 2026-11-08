# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned libslirp source preparation must reject drift before writing outputs."""
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from tools.dev.prepare_slirp import prepare, replace_once

class SlirpProfileTests(unittest.TestCase):
    def test_anchor_requires_exactly_one_match(self):
        self.assertEqual(replace_once('one needle end','needle','new'),'one new end')
        for text in ('absent','needle needle'):
            with self.assertRaises(ValueError):
                replace_once(text,'needle','new')

    def test_hash_mismatch_creates_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);archive=root/'wrong.tar.gz';archive.write_bytes(b'wrong')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                prepare(archive,root/'output')
            self.assertFalse((root/'output').exists())

    def test_selected_unsafe_members_are_rejected_before_writing(self):
        for name,link in [('src/../../escape.c',False),('src/linked.h',True)]:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);archive=root/'source.tar.gz'
                with tarfile.open(archive,'w:gz') as tar:
                    m=tarfile.TarInfo(name)
                    if link:m.type=tarfile.SYMTYPE;m.linkname='/tmp/escape'
                    else:m.size=1
                    tar.addfile(m,None if link else io.BytesIO(b'x'))
                digest=hashlib.sha256(archive.read_bytes()).hexdigest()
                with patch('tools.dev.prepare_slirp.SHA256',digest),self.assertRaises(ValueError):
                    prepare(archive,root/'output')
                self.assertFalse((root/'output').exists())

    def test_real_pinned_source_keeps_notices_and_removes_spawn_and_resolver(self):
        archive=Path('third_party/libslirp/libslirp-4.9.7.tar.gz')
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'source';prepare(archive,output)
            self.assertIn('Redistribution',(output/'LICENSE').read_text())
            self.assertGreater((output/'COPYRIGHT').stat().st_size,0)
            self.assertNotIn('g_spawn_async',(output/'src/misc.c').read_text())
            self.assertNotIn('resolv.conf',(output/'src/slirp.c').read_text())
            self.assertNotIn('getaddrinfo',(output/'src/bootp.c').read_text())
            # Guest packets must not reach host files or the process environment.
            tftp=(output/'src/tftp.c').read_text()
            for host_call in ('open(','stat(','lseek(','read('):self.assertNotIn(host_call,tftp)
            self.assertNotIn('getenv(',(output/'src/tcp_input.c').read_text())
            # if_encap's stack buffer must stay small; see the phat crash record.
            self.assertIn('#define IF_MTU_MAX 1500',(output/'src/if.h').read_text())
            with self.assertRaises(ValueError):prepare(archive,output)

if __name__=='__main__':unittest.main()
