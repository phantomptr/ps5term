# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned SFTP interoperability: temporary files and loopback only."""
import os
from pathlib import Path
import subprocess
import sys
import struct
import time
from paramiko.sftp import CMD_OPENDIR, CMD_CLOSE, CMD_HANDLE, CMD_READDIR, CMD_NAME, CMD_READ, CMD_DATA, int64
import unittest
import paramiko
import test_owned_service as lifecycle

class SftpTest(unittest.TestCase):
    setUp=lifecycle.ServiceTest.setUp
    tearDown=lifecycle.ServiceTest.tearDown
    start=lifecycle.ServiceTest.start
    stop=lifecycle.ServiceTest.stop
    connect=lifecycle.ServiceTest.connect
    install_key=lifecycle.ServiceTest.install_key
    def sftp(self):
        t=self.connect();ch=t.open_session(timeout=5);ch.settimeout(5)
        ch.invoke_subsystem('sftp');s=paramiko.SFTPClient(ch);self.addCleanup(s.close)
        return s
    def test_transfer_metadata_and_paged_listing(self):
        directory=self.root/'files';directory.mkdir()
        for i in range(75):(directory/f'entry-{i:03}').write_text(str(i))
        s=self.sftp();s.chdir(str(directory))
        self.assertEqual(set(s.listdir('.')),{f'entry-{i:03}' for i in range(75)})
        kind,message=s._request(CMD_OPENDIR,str(directory));self.assertEqual(kind,CMD_HANDLE)
        handle=message.get_binary()
        try:
            kind,message=s._request(CMD_READDIR,handle);self.assertEqual(kind,CMD_NAME)
            self.assertGreater(message.get_int(),0)
            # Inspect a fresh page separately: no unbounded full-directory reply.
            kind,message=s._request(CMD_READDIR,handle);self.assertEqual(kind,CMD_NAME)
            self.assertLessEqual(message.get_int(),32)
        finally:s._request(CMD_CLOSE,handle)
        data=bytes(range(256))*1024
        with s.open('transfer','wb') as f:f.write(data)
        s.chmod('transfer',0o600)
        self.assertEqual(s.stat('transfer').st_size,len(data))
        self.assertEqual(s.stat('transfer').st_mode&0o777,0o600)
        s.rename('transfer','renamed')
        with s.open('renamed','rb') as f:self.assertEqual(f.read(),data)
        s.remove('renamed');s.mkdir('directory');s.rmdir('directory')
        self.assertNotIn('renamed',s.listdir('.'))
    def test_independent_working_directories(self):
        a=self.root/'a';b=self.root/'b';a.mkdir();b.mkdir()
        (a/'value').write_bytes(b'A');(b/'value').write_bytes(b'B')
        first=self.sftp();second=self.sftp();first.chdir(str(a));second.chdir(str(b))
        for s,value in ((first,b'A'),(second,b'B')):
            with s.open('value') as f:self.assertEqual(f.read(),value)
        # SFTP must not chdir the process or alter another command session.
        t=self.connect();out,err,status=lifecycle.ServiceTest.run_command(self,t,'pwd')
        self.assertEqual((out,err,status),(b'/data\n',b'',0))
    def test_file_handle_bound_and_release(self):
        s=self.sftp();path=str(self.root/'bounded');handles=[]
        try:
            for _ in range(4):handles.append(s.open(path,'wb'))
            with self.assertRaises(OSError):s.open(path,'wb')
            handles.pop().close()
            with s.open(path,'wb') as f:f.write(b'released')
        finally:
            for f in handles:f.close()
        with s.open(path,'rb') as f:self.assertEqual(f.read(),b'released')
    def test_directory_handle_bound_and_release(self):
        s=self.sftp();handles=[]
        try:
            for _ in range(4):
                kind,message=s._request(CMD_OPENDIR,str(self.root))
                self.assertEqual(kind,CMD_HANDLE);handles.append(message.get_binary())
            with self.assertRaises(OSError):s._request(CMD_OPENDIR,str(self.root))
            s._request(CMD_CLOSE,handles.pop())
            self.assertIn('ssh',s.listdir(str(self.root)))
        finally:
            for handle in handles:s._request(CMD_CLOSE,handle)
    def test_oversized_packet_closes_only_offending_connection(self):
        s=self.sftp();s.sock.sendall(struct.pack('>IBI',0x7fffffff,16,1))
        deadline=time.monotonic()+5
        while not s.sock.closed and time.monotonic()<deadline:time.sleep(.02)
        self.assertTrue(s.sock.closed,'Oversized request did not close promptly')
        t=self.connect();self.assertTrue(t.is_authenticated())
    def test_disconnect_releases_resources(self):
        # Repeated interrupted transfers must not consume the server connection
        # or descriptor slots permanently.
        for index in range(8):
            t=self.connect();ch=t.open_session(timeout=5);ch.settimeout(5)
            ch.invoke_subsystem('sftp');s=paramiko.SFTPClient(ch)
            files=[s.open(str(self.root/f'interrupted-{index}'),'wb') for _ in range(4)]
            t.close()
            for f in files:
                try:f.close()
                except (OSError,EOFError):pass
            time.sleep(.03)
        s=self.sftp()
        with s.open(str(self.root/'after-disconnect'),'wb') as f:f.write(b'ok')
    def test_slow_reader_preserves_window_backpressure(self):
        path=self.root/'slow-reader';data=bytes(range(256))*512;path.write_bytes(data)
        t=self.connect();ch=t.open_session(window_size=32768,max_packet_size=32768,timeout=5)
        ch.settimeout(5);ch.invoke_subsystem('sftp');s=paramiko.SFTPClient(ch);self.addCleanup(s.close)
        with s.open(str(path),'rb') as f:
            requests=[s._async_request(type(None),CMD_READ,f.handle,int64(offset),32768)
                      for offset in range(0,len(data),32768)]
            time.sleep(.2) # Let server fill the receive window before draining.
            received={}
            for _ in requests:
                kind,payload=s._read_packet();self.assertEqual(kind,CMD_DATA)
                message=paramiko.Message(payload);identifier=message.get_int()
                received[identifier]=message.get_binary()
            self.assertEqual(b''.join(received[i] for i in requests),data)
    def test_actual_openssh_sftp_batch(self):
        key=paramiko.ECDSAKey.generate(bits=256);self.install_key(key)
        private=self.root/'sftp-client';key.write_private_key_file(str(private));private.chmod(0o600)
        t=self.connect();host=t.get_remote_server_key();t.close()
        known=self.root/'known-hosts';known.write_text(f'[127.0.0.1]:{self.port} {host.get_name()} {host.get_base64()}\n')
        upload=self.root/'upload';upload.write_bytes(bytes(range(256))*1024)
        remote=self.root/'remote';download=self.root/'download';batch=self.root/'batch'
        batch.write_text(f'put {upload} {remote}\nget {remote} {download}\nrm {remote}\nbye\n')
        result=subprocess.run(['sftp','-q','-b',str(batch),'-P',str(self.port),'-i',str(private),
            '-o','IdentitiesOnly=yes','-o','IdentityAgent=none','-o','ConnectTimeout=5',
            '-o','StrictHostKeyChecking=yes','-o',f'UserKnownHostsFile={known}',
            '-o','GlobalKnownHostsFile=/dev/null','ps5term@127.0.0.1'],stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertEqual(download.read_bytes(),upload.read_bytes());self.assertFalse(remote.exists())

if __name__=='__main__':unittest.main()
