# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual OpenSSH hybrid-KEX and classical-fallback interoperability, loopback only.
Temporary passwords stay in process environments; no console access/config edits.
"""
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
class HybridKexTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        supported=subprocess.check_output(['ssh','-Q','kex'],text=True).splitlines()
        if 'mlkem768x25519-sha256' not in supported:
            raise unittest.SkipTest('OpenSSH 9.9+ with ML-KEM is required')
        cls.temp=tempfile.TemporaryDirectory(prefix='ps5term-pq-');cls.root=Path(cls.temp.name)
        key=cls.root/'host.der'
        subprocess.run(['openssl','ecparam','-name','prime256v1','-genkey','-noout','-outform','DER','-out',str(key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        key.chmod(0o600)
        askpass=cls.root/'askpass';askpass.write_text('#!/bin/sh\nprintf "%s\\n" "$PS5TERM_TEST_PASSWORD"\n');askpass.chmod(0o700)
        cls.env=dict(os.environ,PS5TERM_TEST_PASSWORD=secrets.token_urlsafe(24),SSH_ASKPASS=str(askpass),SSH_ASKPASS_REQUIRE='force',DISPLAY=':0')
        binary=os.environ.get('PS5TERM_SSH_TEST_BINARY',str(ROOT/'build'/('owned-ssh-host-'+sys.platform)/'host-server'))
        cls.server=subprocess.Popen([binary,str(key),str(cls.root)],env=cls.env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        cls.port=int(cls.server.stdout.readline().strip())
    @classmethod
    def tearDownClass(cls):
        _,errors=cls.server.communicate('stop\n',timeout=5)
        cls.temp.cleanup();cls.env.clear()
        if cls.server.returncode:raise AssertionError('Fixture cleanup failed: '+errors)
    def connect(self,kex=None):
        options=['ssh','-F','/dev/null','-T','-v','-p',str(self.port),'-o','ConnectTimeout=5',
                 '-o','IdentityAgent=none','-o','PubkeyAuthentication=no','-o','PreferredAuthentications=password',
                 '-o','NumberOfPasswordPrompts=1','-o','StrictHostKeyChecking=no','-o','UserKnownHostsFile=/dev/null']
        if kex:options+=['-o','KexAlgorithms='+kex]
        result=subprocess.run([*options,'ps5term@127.0.0.1','echo pq-ok'],env=self.env,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,'pq-ok\n')
        return result.stderr
    def test_forced_hybrid(self):
        log=self.connect('mlkem768x25519-sha256')
        self.assertIn('kex: algorithm: mlkem768x25519-sha256',log)
        self.assertNotIn('WARNING: connection is not using a post-quantum',log)
    def test_default_prefers_hybrid(self):
        log=self.connect()
        self.assertIn('kex: algorithm: mlkem768x25519-sha256',log)
        self.assertNotIn('WARNING: connection is not using a post-quantum',log)
    def test_classical_client_fallback(self):
        self.assertIn('kex: algorithm: curve25519-sha256',self.connect('curve25519-sha256'))
if __name__=='__main__':unittest.main(verbosity=2)
