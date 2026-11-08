# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned service lifecycle and real OpenSSH interoperability; loopback only."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import paramiko
ROOT=Path(__file__).resolve().parents[2]
class ServiceTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='ps5term-service-interop-')
        self.root=Path(self.temp.name)
        self.directory=self.root/'ssh'
        self.binary=Path(os.environ.get('PS5TERM_SSH_SERVICE_TEST_BINARY', str(ROOT/'build'/f'owned-ssh-host-{sys.platform}'/'service-server')))
        self.server=None
        self.start()
        self.password=(self.directory/'password').read_text()
    def start(self):
        self.server=subprocess.Popen([str(self.binary),str(self.directory)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        line=self.server.stdout.readline().strip()
        self.assertTrue(line.isdigit(),self.server.stderr.read() if self.server.poll() is not None else 'No port')
        self.port=int(line)
    def stop(self):
        if self.server is None: return
        _,err=self.server.communicate(input='stop\n',timeout=5)
        self.assertEqual(self.server.returncode,0,err)
        self.server=None
    def tearDown(self):
        self.stop(); self.temp.cleanup()
    def connect(self):
        t=paramiko.Transport(('127.0.0.1',self.port))
        self.addCleanup(t.close)
        t.start_client(timeout=5); t.auth_password('ps5term',self.password)
        return t
    def linux_session(self):
        client=paramiko.SSHClient();client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect('127.0.0.1',port=self.port,username='linux',password=self.password,timeout=8,
                       look_for_keys=False,allow_agent=False)
        self.addCleanup(client.close)
        return client

    def test_linux_login_gets_guest_pty_and_passes_bytes_unchanged(self):
        client=self.linux_session()
        channel=client.get_transport().open_session(timeout=8);channel.settimeout(10)
        channel.get_pty(term='xterm-ghostty',width=132,height=43)
        channel.invoke_shell()
        output=b''
        # Booting takes a moment; the client is told rather than left in silence.
        while b'size=132x43' not in output: output+=channel.recv(4096)
        self.assertIn(b'Starting Linux',output)
        self.assertIn(b'term=xterm-ghostty',output)
        channel.sendall(b'abc\x03\r')
        output=b''
        while b'ABC\x03\r' not in output: output+=channel.recv(4096)
        channel.resize_pty(width=90,height=30)
        output=b''
        while b'resized=90x30' not in output: output+=channel.recv(4096)

    def test_linux_exec_status_and_end_of_input(self):
        client=self.linux_session()
        channel=client.get_transport().open_session(timeout=8);channel.settimeout(10)
        channel.exec_command('fail')
        channel.sendall(b'data');channel.shutdown_write()
        output=b''
        while data:=channel.recv(4096): output+=data
        self.assertIn(b'ran:fail',output)
        self.assertIn(b'DATA',output)
        self.assertIn(b'stdin-eof',output)
        self.assertEqual(channel.recv_exit_status(),7)

    def test_long_linux_command_with_a_terminal_like_warp(self):
        # Warp runs `ssh -t host '<several KiB bootstrap>'`: a long command on a PTY.
        client=self.linux_session()
        channel=client.get_transport().open_session(timeout=8);channel.settimeout(10)
        channel.get_pty('xterm-256color',100,30)
        command=': '+'x'*9000+'\necho bootstrap-end'
        channel.exec_command(command)
        output=b''
        while data:=channel.recv(4096): output+=data
        self.assertIn(f'ran {len(command)} bytes ending:{command[-16:]}\n'.encode(),output)
        self.assertIn(b'on a terminal',output)
        self.assertEqual(channel.recv_exit_status(),0)
    def test_warp_bootstrap_on_the_ps5_shell_opens_the_interactive_shell(self):
        # Warp's wrapper sends a POSIX bootstrap that just starts unknown shells.
        bootstrap="\nexport TERM_PROGRAM='WarpTerminal'\n"+': '+'x'*5000+'\nexec "$SHELL"\n'
        channel=self.connect().open_session(timeout=8);channel.settimeout(10)
        channel.get_pty('xterm-256color',100,30)
        channel.exec_command(bootstrap)
        output=b''
        while b'$ ' not in output: output+=channel.recv(4096)
        channel.sendall(b'echo warp-ok\r')
        while b'warp-ok\r\n' not in output: output+=channel.recv(4096)
        channel.sendall(b'exit\r')
        self.assertEqual(channel.recv_exit_status(),0)
        # Without a terminal it is just an over-long command.
        plain=self.connect().open_session(timeout=8)
        with self.assertRaises(paramiko.SSHException): plain.exec_command(bootstrap)
    def test_overlong_commands_are_refused(self):
        client=self.linux_session()
        channel=client.get_transport().open_session(timeout=8)
        with self.assertRaises(paramiko.SSHException): channel.exec_command('x'*16385)
        native=self.connect().open_session(timeout=8)
        with self.assertRaises(paramiko.SSHException): native.exec_command('echo '+'x'*600)
    def test_linux_channels_reuse_without_pty_or_eof_state_leaking(self):
        transport=self.linux_session().get_transport()
        first=transport.open_session(timeout=5);first.settimeout(5)
        first.get_pty(term='xterm-ghostty',width=132,height=43)
        first.invoke_shell()
        output=b''
        while b'size=132x43' not in output: output+=first.recv(4096)
        first.close()
        import time
        time.sleep(0.15)
        for command,status in [('fail',7),('success',0)]:
            channel=transport.open_session(timeout=5);channel.settimeout(5)
            channel.exec_command(command);channel.shutdown_write()
            output=channel.makefile('rb').read()
            self.assertIn(b'term= size=80x24',output)
            self.assertIn(('ran:'+command).encode(),output)
            self.assertEqual(output.count(b'stdin-eof'),1)
            self.assertEqual(channel.recv_exit_status(),status)
            channel.close()

    def test_parallel_linux_channels_route_resize_and_exec_independently(self):
        transport=self.linux_session().get_transport()
        first=transport.open_session(timeout=5);first.settimeout(5)
        first.get_pty(term='xterm-ghostty',width=132,height=43);first.invoke_shell()
        second=transport.open_session(timeout=5);second.settimeout(5)
        second.get_pty(term='xterm-256color',width=90,height=30);second.invoke_shell()
        for channel,expected in [(first,b'term=xterm-ghostty size=132x43'),
                                  (second,b'term=xterm-256color size=90x30')]:
            output=b''
            while expected not in output:output+=channel.recv(4096)
        first.resize_pty(width=77,height=22)
        second.sendall(b'separate-channel')
        output=b''
        while b'SEPARATE-CHANNEL' not in output:output+=second.recv(4096)
        self.assertNotIn(b'resized=77x22',output)
        output=b''
        while b'resized=77x22' not in output:output+=first.recv(4096)
        first.close()
        # An exec sibling must finish without ending the remaining terminal.
        channel=transport.open_session(timeout=5);channel.settimeout(5)
        channel.exec_command('fail');channel.shutdown_write()
        self.assertIn(b'ran:fail',channel.makefile('rb').read())
        self.assertEqual(channel.recv_exit_status(),7);channel.close()
        second.sendall(b'still-running');output=b''
        while b'STILL-RUNNING' not in output:output+=second.recv(4096)
        second.close()

    def test_stalled_closing_channel_is_dropped_alone(self):
        import time
        transport=self.linux_session().get_transport()
        slow=transport.open_session(window_size=32768,max_packet_size=4096,timeout=5)
        slow.settimeout(30)
        # Linux stops while this channel's reader is stalled with output still queued.
        slow.exec_command('vanish');slow.shutdown_write()
        time.sleep(12)  # past the ten-second close deadline
        self.assertTrue(transport.is_active())
        channel=transport.open_session(timeout=5);channel.settimeout(5)
        channel.exec_command('success');channel.shutdown_write()
        self.assertIn(b'ran:success',channel.makefile('rb').read())
        self.assertEqual(channel.recv_exit_status(),0);channel.close()
        received=slow.makefile('rb').read()
        self.assertLess(len(received),48000)
        self.assertEqual(slow.recv_exit_status(),255)
        slow.close()
        self.assertTrue(transport.is_active())

    def test_broken_guest_link_fails_fast_with_guidance(self):
        import time
        transport=self.linux_session().get_transport()
        started=time.monotonic()
        channel=transport.open_session(timeout=5);channel.settimeout(10)
        channel.exec_command('linkfail');channel.shutdown_write()
        output=channel.makefile('rb').read()
        self.assertIn(b'link to Linux failed',output)
        self.assertEqual(channel.recv_exit_status(),1)
        self.assertLess(time.monotonic()-started,5)
        channel.close()

    def test_failed_passwords_delay_only_the_next_handshake_from_that_address(self):
        import time
        bad=paramiko.Transport(('127.0.0.1',self.port));self.addCleanup(bad.close)
        bad.start_client(timeout=5)
        with self.assertRaises(paramiko.AuthenticationException):
            bad.auth_password('ps5term','wrong-password')
        bad.close()
        time.sleep(0.3)
        started=time.monotonic()
        good=self.connect()  # held until the backoff ends, then served normally
        self.assertGreater(time.monotonic()-started,0.5)  # one failure: one second
        channel=good.open_session(timeout=5);channel.settimeout(5)
        channel.exec_command('pwd');channel.shutdown_write()
        self.assertEqual(channel.makefile('rb').read(),b'/data\n')
        channel.close()

    def test_linux_eof_follows_all_buffered_input(self):
        transport=self.linux_session().get_transport()
        channel=transport.open_session(timeout=5);channel.settimeout(10)
        channel.exec_command('cat');channel.sendall(b'z'*100000);channel.shutdown_write()
        output=channel.makefile('rb').read()
        self.assertEqual(output.count(b'Z'),100000)
        self.assertTrue(output.endswith(b'Zstdin-eof\n'),output[-100:])
        self.assertEqual(channel.recv_exit_status(),0)

    def test_openssh_controlmaster_sequential_commands(self):
        self.controlmaster_commands(False)

    def test_openssh_controlmaster_concurrent_shell_and_exec(self):
        self.controlmaster_commands(True)

    def controlmaster_commands(self, hold_shell):
        import time,select
        key=paramiko.ECDSAKey.generate(bits=256)
        self.install_key(key)
        identity=self.root/'client-key';key.write_private_key_file(str(identity));identity.chmod(0o600)
        control=tempfile.TemporaryDirectory(prefix='pt-',dir='/tmp')
        self.addCleanup(control.cleanup)
        socket=Path(control.name)/'mux'
        options=['ssh','-F','/dev/null','-p',str(self.port),'-i',str(identity),
                 '-o','BatchMode=yes','-o','IdentitiesOnly=yes',
                 '-o','StrictHostKeyChecking=no','-o','UserKnownHostsFile=/dev/null',
                 '-o','ConnectTimeout=5','-S',str(socket)]
        master=subprocess.Popen([*options,'-M','-N','ps5term@127.0.0.1'],
                                stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        holder=None
        try:
            deadline=time.monotonic()+5
            while not socket.exists() and master.poll() is None and time.monotonic()<deadline:
                time.sleep(0.01)
            self.assertTrue(socket.exists(),'OpenSSH master did not create its control socket')
            if hold_shell:
                holder=subprocess.Popen([*options,'-o','ControlMaster=no','-o','ProxyCommand=false',
                    '-T','ps5term@127.0.0.1'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                holder.stdin.write(b'echo holder-ready\n');holder.stdin.flush()
                output=b'';deadline=time.monotonic()+5
                while b'\nholder-ready\n' not in output and time.monotonic()<deadline:
                    if select.select([holder.stdout],[],[],.1)[0]:
                        data=os.read(holder.stdout.fileno(),4096)
                        if not data:break
                        output+=data
                self.assertIn(b'\nholder-ready\n',output)
            for index in range(3):
                # Refuse a new TCP connection: success must use the master.
                command=[*options,'-o','ControlMaster=no','-o','ProxyCommand=false',
                         'ps5term@127.0.0.1','echo reused']
                result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=8)
                self.assertEqual((result.returncode,result.stdout),(0,b'reused\n'),result.stderr)
                self.assertIsNone(master.poll(),'SSH transport exited between sessions')
                if holder:self.assertIsNone(holder.poll(),'Existing shell ended during sibling exec')
                if index==0 and not hold_shell:
                    # A completed channel must clear the service's ten-second
                    # close deadline while retaining the authenticated master.
                    time.sleep(11)
            self.assertEqual(subprocess.run([*options,'-O','check','ps5term@127.0.0.1'],
                             capture_output=True,timeout=5).returncode,0)
        finally:
            if holder:
                holder.terminate();holder.communicate(timeout=5)
            master.terminate()
            master.communicate(timeout=5)

    def test_authenticated_exec_and_stop_disconnects(self):
        t=self.connect(); ch=t.open_session(timeout=5);ch.settimeout(5)
        ch.exec_command('echo lifecycle'); ch.shutdown_write()
        self.assertEqual(ch.makefile('rb').read(),b'lifecycle\n')
        self.assertEqual(ch.recv_exit_status(),0)
        live=self.connect(); channel=live.open_session(timeout=5);channel.settimeout(5);channel.invoke_shell()
        self.stop()
        while channel.recv(4096): pass
        self.assertTrue(channel.closed or channel.eof_received)
    def test_password_change_revokes_sessions_and_old_login(self):
        live=self.connect(); key=live.get_remote_server_key().asbytes()
        channel=live.open_session(timeout=5); channel.settimeout(5); channel.invoke_shell()
        old=self.password
        self.server.stdin.write('password replacement passphrase\n'); self.server.stdin.flush()
        self.assertEqual(self.server.stdout.readline().strip(),'saved')
        while channel.recv(4096): pass
        self.assertTrue(channel.closed or channel.eof_received)
        denied=paramiko.Transport(('127.0.0.1',self.port)); self.addCleanup(denied.close)
        denied.start_client(timeout=5)
        with self.assertRaises(paramiko.AuthenticationException): denied.auth_password('ps5term',old)
        denied.close()
        self.password='replacement passphrase'
        self.assertEqual(self.connect().get_remote_server_key().asbytes(),key)
        self.stop(); self.start()
        self.assertEqual((self.directory/'password').read_text(),self.password)
        self.assertTrue(self.connect().is_authenticated())
    def test_restart_preserves_host_key_and_password(self):
        key=self.connect().get_remote_server_key().asbytes()
        self.stop(); self.start()
        self.assertEqual((self.directory/'password').read_text(),self.password)
        self.assertEqual(self.connect().get_remote_server_key().asbytes(),key)
    def install_key(self, key):
        self.stop()
        path=self.directory/'authorized_keys'
        path.write_text(f'{key.get_name()} {key.get_base64()} test-key\n')
        path.chmod(0o600)
        self.start()
    def test_public_key_authentication_and_rejection(self):
        key=paramiko.ECDSAKey.generate(bits=256)
        self.install_key(key)
        t=paramiko.Transport(('127.0.0.1',self.port)); self.addCleanup(t.close)
        t.start_client(timeout=5); t.auth_publickey('ps5term',key)
        ch=t.open_session(timeout=5); ch.settimeout(5); ch.exec_command('echo key-login');ch.shutdown_write()
        self.assertEqual(ch.makefile('rb').read(),b'key-login\n')
        self.assertEqual(ch.recv_exit_status(),0)
        for user, candidate in [('root',key),('ps5term',paramiko.ECDSAKey.generate(bits=256))]:
            with self.subTest(user=user):
                other=paramiko.Transport(('127.0.0.1',self.port));self.addCleanup(other.close)
                other.start_client(timeout=5)
                with self.assertRaises(paramiko.AuthenticationException): other.auth_publickey(user,candidate)
        # Password remains available with keys configured.
        self.assertTrue(self.connect().is_authenticated())
    def run_command(self,transport,command):
        ch=transport.open_session(timeout=5);ch.settimeout(5);ch.exec_command(command);ch.shutdown_write()
        result=(ch.makefile('rb').read(),ch.makefile_stderr('rb').read(),ch.recv_exit_status())
        ch.close();return result
    def test_authenticated_key_enrollment_and_revocation(self):
        key=paramiko.ECDSAKey.generate(bits=256)
        text=f'{key.get_name()} {key.get_base64()}'
        t=self.connect()
        self.assertEqual(self.run_command(t,'ssh-key-add '+text)[2],0)
        self.assertEqual((self.directory/'authorized_keys').stat().st_mode&0o777,0o600)
        t.close();self.stop();self.start()
        enrolled=paramiko.Transport(('127.0.0.1',self.port));self.addCleanup(enrolled.close)
        enrolled.start_client(timeout=5);enrolled.auth_publickey('ps5term',key)
        self.assertEqual(self.run_command(enrolled,'ssh-key-remove '+text)[2],0)
        denied=paramiko.Transport(('127.0.0.1',self.port));self.addCleanup(denied.close)
        denied.start_client(timeout=5)
        with self.assertRaises(paramiko.AuthenticationException):denied.auth_publickey('ps5term',key)
        self.assertTrue(self.connect().is_authenticated())
    def test_key_enrollment_collision_and_idempotence(self):
        key=paramiko.ECDSAKey.generate(bits=256);text=f'{key.get_name()} {key.get_base64()}'
        temporary=self.directory/'authorized_keys.update';temporary.symlink_to('password')
        password=(self.directory/'password').read_bytes()
        t=self.connect();self.assertNotEqual(self.run_command(t,'ssh-key-add '+text)[2],0);t.close()
        self.assertEqual((self.directory/'password').read_bytes(),password)
        self.assertFalse((self.directory/'authorized_keys').exists());temporary.unlink()
        for _ in range(2):
            t=self.connect();self.assertEqual(self.run_command(t,'ssh-key-add '+text)[2],0);t.close()
        self.assertEqual((self.directory/'authorized_keys').read_text(),text+'\n')
    def test_key_enrollment_refuses_invalid_or_changed_file(self):
        key=paramiko.ECDSAKey.generate(bits=256)
        text=f'{key.get_name()} {key.get_base64()}'
        t=self.connect()
        self.assertNotEqual(self.run_command(t,'ssh-key-add restrict '+text)[2],0);t.close()
        self.assertFalse((self.directory/'authorized_keys').exists())
        # File edited after service startup must be reloaded, not overwritten.
        path=self.directory/'authorized_keys';path.write_text(text+'\n');path.chmod(0o600)
        original=path.read_bytes()
        t=self.connect();self.assertNotEqual(self.run_command(t,'ssh-key-remove '+text)[2],0);t.close()
        self.assertEqual(path.read_bytes(),original)
    def test_authorized_key_file_bounds_and_restrictions(self):
        key=paramiko.ECDSAKey.generate(bits=256)
        good=f'{key.get_name()} {key.get_base64()} comment\n'
        self.stop()
        path=self.directory/'authorized_keys'
        # Eight valid records, comments, whitespace, CRLF and no final newline.
        path.write_text('# keys\n\n'+(' \t'+good.rstrip()+'\r\n')*7+good.rstrip())
        path.chmod(0o600); self.start()
        t=paramiko.Transport(('127.0.0.1',self.port)); self.addCleanup(t.close)
        t.start_client(timeout=5);t.auth_publickey('ps5term',key)
        self.assertTrue(t.is_authenticated());t.close();self.stop()
        import base64,struct
        wire=key.asbytes()
        # Malformed files disable startup instead of dropping restrictions or
        # partially accepting a prefix of the file.
        invalid=[good*9, good+'restrict '+good, good+'ssh-rsa AAAA\n',
                 good.replace('nistp256','nistp384',1),
                 f'{key.get_name()} {base64.b64encode(wire+b"extra").decode()}\n',
                 f'{key.get_name()} {base64.b64encode(struct.pack(">I",0xffffffff)).decode()}\n']
        for text in invalid:
            with self.subTest(case=invalid.index(text)):
                path.write_text(text)
                failed=subprocess.run([str(self.binary),str(self.directory)],input='stop\n',
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=5)
                self.assertEqual(failed.returncode,3)
                self.assertEqual(failed.stdout,'')
        path.unlink();self.start()
        self.assertTrue(self.connect().is_authenticated())
    def test_authorized_public_key_requires_matching_signature(self):
        key=paramiko.ECDSAKey.generate(bits=256)
        self.install_key(key)
        # Offer an allowed public key but sign with an unrelated private key.
        # The allowlist callback must not bypass wolfSSH signature verification.
        key.sign_ssh_data=paramiko.ECDSAKey.generate(bits=256).sign_ssh_data
        t=paramiko.Transport(('127.0.0.1',self.port)); self.addCleanup(t.close)
        t.start_client(timeout=5)
        with self.assertRaises(paramiko.AuthenticationException): t.auth_publickey('ps5term',key)
    def test_ed25519_public_key(self):
        identity=self.root/'ed25519-client'
        subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-f',str(identity)],check=True,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=5)
        key=paramiko.Ed25519Key.from_private_key_file(str(identity))
        self.install_key(key)
        t=paramiko.Transport(('127.0.0.1',self.port)); self.addCleanup(t.close)
        t.start_client(timeout=5); t.auth_publickey('ps5term',key)
        ch=t.open_session(timeout=5); ch.settimeout(5); ch.exec_command('echo ed25519');ch.shutdown_write()
        self.assertEqual(ch.makefile('rb').read(),b'ed25519\n')
        self.assertEqual(ch.recv_exit_status(),0)
    def test_real_openssh_public_key_and_restart(self):
        key=paramiko.ECDSAKey.generate(bits=384)
        self.install_key(key)
        identity=self.root/'client-key'; key.write_private_key_file(str(identity)); identity.chmod(0o600)
        server_key=self.connect().get_remote_server_key()
        self.stop(); self.start()
        known=self.root/'known-key-hosts'
        known.write_text(f'[127.0.0.1]:{self.port} {server_key.get_name()} {server_key.get_base64()}\n')
        result=subprocess.run(['ssh','-T','-p',str(self.port),'-i',str(identity),
            '-o','BatchMode=yes','-o','IdentitiesOnly=yes','-o','IdentityAgent=none',
            '-o','PreferredAuthentications=publickey','-o','ConnectTimeout=5',
            '-o','StrictHostKeyChecking=yes','-o',f'UserKnownHostsFile={known}',
            '-o','GlobalKnownHostsFile=/dev/null','ps5term@127.0.0.1','echo openssh-key'],
            stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertEqual(result.stdout,b'openssh-key\n')
    def test_real_openssh_exec_for_terminal_clients(self):
        t=self.connect(); key=t.get_remote_server_key()
        known=self.root/'known_hosts'
        known.write_text(f'[127.0.0.1]:{self.port} {key.get_name()} {key.get_base64()}\n')
        askpass=self.root/'askpass'
        askpass.write_text('#!/bin/sh\nprintf "%s\\n" "$PS5TERM_TEST_PASSWORD"\n'); askpass.chmod(0o700)
        for terminal in ('xterm-256color','xterm-ghostty','screen-256color','tmux-256color'):
            self.check_openssh(terminal,known,askpass)
    def test_person_typing_the_password_slowly_still_logs_in(self):
        # People read the prompt and type; 15 seconds from connect was not enough.
        t=self.connect(); key=t.get_remote_server_key()
        known=self.root/'known_hosts'
        known.write_text(f'[127.0.0.1]:{self.port} {key.get_name()} {key.get_base64()}\n')
        askpass=self.root/'askpass-slow'
        askpass.write_text('#!/bin/sh\nsleep 20\nprintf "%s\\n" "$PS5TERM_TEST_PASSWORD"\n'); askpass.chmod(0o700)
        result=subprocess.run(['ssh','-T','-p',str(self.port),'-o','ConnectTimeout=5','-o','StrictHostKeyChecking=yes',
            '-o',f'UserKnownHostsFile={known}','-o','GlobalKnownHostsFile=/dev/null',
            '-o','PreferredAuthentications=password','-o','PubkeyAuthentication=no',
            '-o','NumberOfPasswordPrompts=1','ps5term@127.0.0.1','echo slow-login'],
            env={**os.environ,'SSH_ASKPASS':str(askpass),'SSH_ASKPASS_REQUIRE':'force','DISPLAY':':0',
                 'PS5TERM_TEST_PASSWORD':self.password},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=40)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertEqual(result.stdout,b'slow-login\n')
    def check_openssh(self,terminal,known,askpass):
        result=subprocess.run(['ssh','-T','-p',str(self.port),'-o','ConnectTimeout=5','-o','StrictHostKeyChecking=yes',
            '-o',f'UserKnownHostsFile={known}','-o','GlobalKnownHostsFile=/dev/null',
            '-o','PreferredAuthentications=password','-o','PubkeyAuthentication=no',
            '-o','NumberOfPasswordPrompts=1','-o',f'SetEnv=TERM={terminal}','ps5term@127.0.0.1','echo openssh'],
            env={**os.environ,'SSH_ASKPASS':str(askpass),'SSH_ASKPASS_REQUIRE':'force','DISPLAY':':0',
                 'PS5TERM_TEST_PASSWORD':self.password},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertEqual(result.stdout,b'openssh\n')
if __name__=='__main__': unittest.main()
