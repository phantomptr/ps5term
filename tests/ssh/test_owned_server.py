# SPDX-License-Identifier: GPL-3.0-or-later
"""Host interoperability contract. No console addresses or payloads involved.

Run with a Python environment containing paramiko==4.0.0 after building
tools/build-owned-ssh-host.py. Temporary credentials never enter repo assets.
"""
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import time
import unittest

import paramiko

ROOT = Path(__file__).resolve().parents[2]


class OwnedServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = Path(os.environ.get('PS5TERM_SSH_TEST_BINARY',
                      ROOT / 'build' / f'owned-ssh-host-{sys.platform}' / 'host-server'))
        if not binary.is_file():
            raise AssertionError('Owned SSH host server has not been built')
        cls.directory = tempfile.TemporaryDirectory(prefix='ps5term-owned-ssh-')
        cls.root = Path(cls.directory.name)
        key = cls.root / 'host.der'
        subprocess.run(['openssl', 'ecparam', '-name', 'prime256v1', '-genkey', '-noout',
                        '-outform', 'DER', '-out', str(key)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        key.chmod(0o600)
        (cls.root / 'large').write_bytes(b'q' * 200000)
        cls.password = secrets.token_urlsafe(24)
        cls.server = subprocess.Popen([str(binary), str(key), str(cls.root)],
                                      env={**os.environ, 'PS5TERM_TEST_PASSWORD': cls.password},
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True)
        cls.port = int(cls.server.stdout.readline().strip())

    @classmethod
    def tearDownClass(cls):
        unexpected = cls.server.poll()
        _, errors = cls.server.communicate(input='stop\n', timeout=5)
        if errors:
            print(errors, end='')
        cls.directory.cleanup()
        if unexpected is not None:
            raise AssertionError(f'Host fixture exited unexpectedly: {unexpected}')
        if cls.server.returncode != 0:
            raise AssertionError(f'Host fixture cleanup failed: {cls.server.returncode}')

    def connect(self, username='ps5term', password=None):
        transport = paramiko.Transport(('127.0.0.1', self.port))
        self.addCleanup(transport.close)
        transport.start_client(timeout=5)
        transport.auth_password(username, self.password if password is None else password)
        return transport

    def execute(self, command):
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        channel.settimeout(5)
        channel.exec_command(command)
        channel.shutdown_write()
        out = channel.makefile('rb').read()
        err = channel.makefile_stderr('rb').read()
        status = channel.recv_exit_status()
        channel.close()
        return out, err, status

    def test_exec_and_stderr_status(self):
        self.assertEqual(self.execute('echo owned'), (b'owned\n', b'', 0))
        out, err, status = self.execute('unknown-command')
        self.assertEqual(out, b'')
        self.assertTrue(err)
        self.assertEqual(status, 127)

    def test_native_storage_summary_over_exec(self):
        blocks = (self.root / 'large').stat().st_blocks
        self.assertEqual(self.execute('du -sk large'),
                         (f'{(blocks + 1) // 2}\tlarge\n'.encode(), b'', 0))

    def test_native_filename_expansion_over_exec(self):
        for name in ('second.log', 'first space.log', '.hidden.log'):
            (self.root / name).write_text('fixture')
        self.assertEqual(self.execute('echo *.log'),
                         (b'first space.log second.log\n', b'', 0))
        self.assertEqual(self.execute('echo "*.log"'), (b'*.log\n', b'', 0))
        out, err, status = self.execute('echo ok > *.blocked')
        self.assertEqual((out, status), (b'', 2))
        self.assertIn(b'wildcard', err)
        self.assertFalse((self.root / '*.blocked').exists())

    def test_sequential_channels_reuse_authenticated_transport(self):
        transport = self.connect()
        for command, expected, status in (
                ('echo reused', b'reused\n', 0),
                ('unknown-command', b'', 127),
                ('pwd', str(self.root).encode() + b'\n', 0)):
            channel = transport.open_session(timeout=5)
            channel.settimeout(5)
            channel.exec_command(command)
            channel.shutdown_write()
            self.assertEqual(channel.makefile('rb').read(), expected)
            errors = channel.makefile_stderr('rb').read()
            self.assertEqual(bool(errors), status != 0)
            self.assertEqual(channel.recv_exit_status(), status)
            channel.close()
            self.assertTrue(transport.is_active())

    def test_closed_shell_releases_session_state(self):
        transport = self.connect()
        first = transport.open_session(timeout=5)
        first.settimeout(5)
        first.get_pty(term='xterm', width=40, height=12)
        first.invoke_shell()
        first.sendall(b'cd /\npwd\n')
        output = b''
        while b'/$ ' not in output:
            output += first.recv(4096)
        first.close()
        # Wait for the close acknowledgement before opening another channel.
        time.sleep(0.15)
        second = transport.open_session(timeout=5)
        second.settimeout(5)
        second.exec_command('pwd')
        second.shutdown_write()
        self.assertEqual(second.makefile('rb').read(), str(self.root).encode() + b'\n')
        self.assertEqual(second.recv_exit_status(), 0)
        second.close()

    def test_rejected_reused_subsystem_does_not_break_transport(self):
        transport = self.connect()
        first = transport.open_session(timeout=5)
        first.settimeout(5)
        first.exec_command('echo first')
        self.assertEqual(first.makefile('rb').read(), b'first\n')
        self.assertEqual(first.recv_exit_status(), 0)
        first.close()
        refused = transport.open_session(timeout=5)
        refused.settimeout(5)
        with self.assertRaises(paramiko.SSHException):
            refused.invoke_subsystem('sftp')
        refused.close()
        time.sleep(0.15)
        last = transport.open_session(timeout=5)
        last.settimeout(5)
        last.exec_command('echo survived')
        self.assertEqual(last.makefile('rb').read(), b'survived\n')
        self.assertEqual(last.recv_exit_status(), 0)
        last.close()

    def test_parallel_shells_and_slot_reclamation(self):
        transport = self.connect()
        first = transport.open_session(timeout=5)
        second = transport.open_session(timeout=5)
        for channel in (first, second):
            channel.settimeout(5)
            channel.invoke_shell()
        first.sendall(b'cd /\npwd\n')
        second.sendall(b'pwd\n')
        first.shutdown_write()
        self.assertIn(b'/$ pwd\r\n/\n', first.makefile('rb').read())
        self.assertEqual(first.recv_exit_status(), 0)
        first.close()
        # Keep the other shell alive while reusing the remaining slots.
        for index in range(12):
            if index == 5:
                transport.renegotiate_keys()
            channel = transport.open_session(timeout=5)
            channel.settimeout(5)
            channel.exec_command('echo peer-' + str(index))
            self.assertEqual(channel.makefile('rb').read(), f'peer-{index}\n'.encode())
            self.assertEqual(channel.recv_exit_status(), 0)
            channel.close()
        second.sendall(b'echo still-independent\n')
        second.shutdown_write()
        output = second.makefile('rb').read()
        self.assertIn(str(self.root).encode() + b'\n', output)
        self.assertIn(b'\nstill-independent\n', output)
        self.assertEqual(second.recv_exit_status(), 0)
        second.close()

    def test_stalled_channel_does_not_block_other_stdout_stderr_or_exit(self):
        transport = self.connect()
        statuses = {}
        transport._channel_handler_table = transport._channel_handler_table.copy()
        original = transport._channel_handler_table[paramiko.common.MSG_CHANNEL_REQUEST]
        def count_status(channel, message):
            kind = paramiko.Message(message.get_remainder()).get_text()
            if kind == 'exit-status':
                statuses[channel.chanid] = statuses.get(channel.chanid, 0) + 1
            return original(channel, message)
        transport._channel_handler_table[paramiko.common.MSG_CHANNEL_REQUEST] = count_status
        slow = transport.open_session(window_size=32768, max_packet_size=4096, timeout=5)
        slow.settimeout(10)
        slow.exec_command('cat large')
        time.sleep(0.2)
        for command, expected, status in [('echo independent', b'independent\n', 0),
                                          ('unknown-command', b'', 127)]:
            channel = transport.open_session(timeout=5)
            channel.settimeout(5)
            channel.exec_command(command)
            self.assertEqual(channel.makefile('rb').read(), expected)
            self.assertEqual(bool(channel.makefile_stderr('rb').read()), status != 0)
            self.assertEqual(channel.recv_exit_status(), status)
            channel.close()
        self.assertEqual(slow.makefile('rb').read(), b'q' * 200000)
        self.assertEqual(slow.recv_exit_status(), 0)
        slow.close()
        self.assertEqual(len(statuses),3)
        self.assertEqual(list(statuses.values()),[1,1,1])

    def test_authentication_rejects_wrong_user_and_password(self):
        with self.assertRaises(paramiko.AuthenticationException):
            self.connect(password='wrong')
        with self.assertRaises(paramiko.AuthenticationException):
            self.connect(username='root')
        self.assertEqual(self.execute('echo still-alive'), (b'still-alive\n', b'', 0))

    def test_streaming_and_slow_reader(self):
        transport = self.connect()
        channel = transport.open_session(window_size=32768, max_packet_size=4096, timeout=5)
        channel.settimeout(10)
        channel.exec_command('cat large')
        time.sleep(0.3)  # exhaust the SSH receive window before reading/adjusting it
        chunks = []
        while data := channel.recv(997):
            chunks.append(data)
        self.assertEqual(b''.join(chunks), b'q' * 200000)  # Exact file bytes over exec.
        self.assertEqual(channel.recv_exit_status(), 0)

    def test_shell_fragmentation_and_eof(self):
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        channel.settimeout(5)
        channel.invoke_shell()  # no PTY: the adapter must not claim one
        channel.sendall(b'echo frag')
        channel.sendall(b'mented\r\npwd\n')
        channel.shutdown_write()
        output = channel.makefile('rb').read()
        self.assertIn(b'fragmented\n', output)
        self.assertIn(str(self.root).encode(), output)
        self.assertEqual(channel.recv_exit_status(), 0)

    def test_pty_gets_terminal_line_endings(self):
        # A client asking for a PTY puts its terminal in raw mode; bare LF would staircase.
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        channel.settimeout(10)
        channel.get_pty()
        channel.invoke_shell()
        channel.sendall(b'echo one; echo two\r')
        output = b''
        while b'\r\ntwo\r\n' not in output:  # Not the echoed command line.
            output += channel.recv(4096)
        self.assertIn(b'one\r\ntwo\r\n', output)
        channel.close()

    def test_reject_unknown_subsystems_forwarding_and_excess_channels(self):
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        with self.assertRaises(paramiko.SSHException):
            channel.invoke_subsystem('unsupported-subsystem')
        transport = self.connect()
        with self.assertRaises(paramiko.ChannelException):
            transport.open_channel('direct-tcpip', ('127.0.0.1', 22), ('127.0.0.1', 0), timeout=5)
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        channel.invoke_shell()
        peers = [transport.open_session(timeout=5) for _ in range(3)]
        with self.assertRaises(paramiko.ChannelException):
            transport.open_session(timeout=5)
        for peer in peers:
            peer.close()
        replacement=transport.open_session(timeout=5);replacement.settimeout(5)
        replacement.exec_command('echo reclaimed')
        self.assertEqual(replacement.makefile('rb').read(),b'reclaimed\n')
        self.assertEqual(replacement.recv_exit_status(),0);replacement.close()
        channel.sendall(b'exit\n')
        self.assertEqual(channel.recv_exit_status(), 0)

    def test_disconnect_during_command_and_reconnect(self):
        transport = self.connect()
        channel = transport.open_session(timeout=5)
        channel.exec_command('cat large')
        transport.close()
        self.assertEqual(self.execute('echo recovered'), (b'recovered\n', b'', 0))

    def test_concurrent_shells_keep_independent_directories(self):
        first = self.connect().open_session(timeout=5)
        second = self.connect().open_session(timeout=5)
        for channel in (first, second):
            channel.settimeout(5)
            channel.invoke_shell()
        first.sendall(b'cd /\npwd\n')
        second.sendall(b'pwd\n')
        first.shutdown_write()
        second.shutdown_write()
        self.assertIn(b'/$ pwd\r\n/\n', first.makefile('rb').read())
        root = str(self.root).encode()
        self.assertIn(root + b'$ pwd\r\n' + root + b'\n', second.makefile('rb').read())
        for channel in (first, second):
            self.assertEqual(channel.recv_exit_status(), 0)
            channel.close()

    def test_eof_drains_input_larger_than_adapter_queue(self):
        channel = self.connect().open_session(timeout=5)
        channel.settimeout(10)
        channel.invoke_shell()
        channel.sendall(b'echo z\n' * 3000)
        channel.shutdown_write()
        output = channel.makefile('rb').read()
        self.assertEqual(output.count(b'\nz\n'), 3000)
        self.assertEqual(channel.recv_exit_status(), 0)


if __name__ == '__main__':
    unittest.main()
