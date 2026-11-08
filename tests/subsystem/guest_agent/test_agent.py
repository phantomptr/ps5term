# SPDX-License-Identifier: GPL-3.0-or-later
"""Linux-only PTY-agent acceptance. No PS5/SSH connection or network sockets."""
import os
import select
import struct
import sys
import subprocess
import time
import unittest

BINARY = os.environ.get('PS5TERM_GUEST_AGENT', '/tmp/ps5term-guest-agent')
OPEN, OPENED, INPUT, INPUT_OK, OUTPUT, CREDIT, RESIZE, CLOSE, EXIT, HELLO, EXEC, INPUT_EOF = range(1, 13)
CONTROL_ID = 0xffffffff


class Agent:
    def __init__(self):
        self.p = subprocess.Popen([BINARY], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        self.buffer = b''
        self.events = []
        # The agent announces itself first, so the host can trust a shutdown request.
        hello = self.receive()
        assert hello[:2] == (HELLO, CONTROL_ID), hello

    def send(self, kind, sid, data=b'', fragment=False):
        frame = struct.pack('!4sBBHII', b'P5GA', 1, kind, 0, sid, len(data)) + data
        if fragment:
            for byte in frame:
                self.p.stdin.write(bytes([byte]))
                self.p.stdin.flush()
        else:
            self.p.stdin.write(frame)
            self.p.stdin.flush()

    def receive(self, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if len(self.buffer) >= 16:
                magic, version, kind, reserved, sid, size = struct.unpack('!4sBBHII', self.buffer[:16])
                assert (magic, version, reserved) == (b'P5GA', 1, 0)
                assert 0 < sid and size <= 4096
                if len(self.buffer) >= 16 + size:
                    payload = self.buffer[16:16 + size]
                    self.buffer = self.buffer[16 + size:]
                    return kind, sid, payload
            if select.select([self.p.stdout], [], [], max(0, end - time.monotonic()))[0]:
                data = os.read(self.p.stdout.fileno(), 16384)
                if not data:
                    raise EOFError(self.p.stderr.read().decode())
                self.buffer += data
        raise TimeoutError('Agent frame deadline')

    def opened(self, sid, cols=91, rows=27, fragment=False):
        self.send(OPEN, sid, struct.pack('!HH', cols, rows) + b'xterm-256color', fragment)
        self.assert_event(OPENED, sid, struct.pack('!I', 4096))
        self.send(CREDIT, sid, struct.pack('!I', 8192))

    def assert_event(self, kind, sid, data=None):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            event = self.receive()
            if event[:2] == (kind, sid):
                if data is not None:
                    assert event[2] == data, event
                return event
            self.events.append(event)
        raise AssertionError((kind, sid))

    def command(self, sid, text):
        self.send(INPUT, sid, text.encode())
        self.assert_event(INPUT_OK, sid, struct.pack('!I', len(text.encode())))

    def output_until(self, sid, marker):
        out = b''
        end = time.monotonic() + 5
        while marker not in out and time.monotonic() < end:
            event = self.events.pop(0) if self.events else self.receive()
            if event[:2] == (OUTPUT, sid):
                out += event[2]
                self.send(CREDIT, sid, struct.pack('!I', len(event[2])))
            else:
                # Preserve other sessions separately to avoid re-reading them.
                self.deferred.append(event)
        assert marker in out, out
        return out

    def stop(self):
        if self.p.stdin and not self.p.stdin.closed:
            self.p.stdin.close()
        try:
            self.p.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
            raise
        for stream in (self.p.stdout, self.p.stderr):
            stream.close()


@unittest.skipUnless(sys.platform.startswith('linux'), 'Requires real Linux PTYs')
class AgentTest(unittest.TestCase):
    def setUp(self):
        self.a = Agent()
        self.a.deferred = []

    def tearDown(self):
        self.a.stop()

    def test_isolated_shells_fragmentation_resize(self):
        a = self.a
        a.opened(1, fragment=True)
        a.opened(2)
        a.command(1, "PS5TERM_TEST=one; printf 'size='; stty size\n")
        a.output_until(1, b'size=27 91')
        a.send(RESIZE, 1, struct.pack('!HH', 112, 35))
        a.command(1, "stty size\n")
        a.output_until(1, b'35 112')
        a.command(2, "printf 'isolation=%s:end\\n' \"${PS5TERM_TEST:-unset}\"\n")
        a.events += a.deferred
        a.deferred = []
        a.output_until(2, b'isolation=unset:end')
        a.command(1, "exit 7\n")
        a.assert_event(EXIT, 1, struct.pack('!I', 7))
        a.command(2, "printf 'alive\\n'\n")
        a.output_until(2, b'alive\r\n')

    def test_exit_allows_inflight_credit_and_close(self):
        a = self.a
        a.opened(1)
        a.opened(2)
        a.command(1, "exit 0\n")
        a.assert_event(EXIT, 1, struct.pack('!I', 0))
        # These can already be on the wire before the host receives EXIT.
        a.send(CREDIT, 1, struct.pack('!I', 1))
        a.send(CLOSE, 1)
        a.command(2, "printf 'still-here\\n'\n")
        a.output_until(2, b'still-here\r\n')

    def test_invalid_term_and_zero_window(self):
        self.a.send(OPEN, 1, struct.pack('!HH', 0, 24) + b'xterm')
        self.assertEqual(self.a.p.wait(timeout=4), 2)

    def test_partial_frame_eof(self):
        self.a.p.stdin.write(b'P5GA')
        self.a.p.stdin.close()
        self.assertEqual(self.a.p.wait(timeout=4), 2)

    def test_binary_pty_io(self):
        a = self.a
        a.opened(1)
        a.command(1, "stty raw -echo; printf 'RAW_READY\\n'; dd bs=1 count=3 2>/dev/null | od -An -tx1; stty sane\n")
        a.output_until(1, b'RAW_READY\n')
        a.send(INPUT, 1, b'\x00\xffA')
        a.assert_event(INPUT_OK, 1, struct.pack('!I', 3))
        a.output_until(1, b'00 ff 41')
        a.command(1, "printf '\\000\\377BINARY_END'\n")
        a.output_until(1, b'\x00\xffBINARY_END')

    def test_output_credit_bound_and_active_disconnect(self):
        a = self.a
        for sid in range(1, 5):
            a.opened(sid)
            a.command(sid, "yes bounded\n")
        totals = {sid: 0 for sid in range(1, 5)}
        pending = a.events
        a.events = []
        while True:
            try:
                event = pending.pop(0) if pending else a.receive(timeout=0.2)
            except TimeoutError:
                break
            self.assertEqual(event[0], OUTPUT)
            totals[event[1]] += len(event[2])
        self.assertEqual(totals, {sid: 8192 for sid in range(1, 5)})
        # Four busy shells blocked on output cannot prevent transport teardown.
        a.p.stdin.close()
        self.assertEqual(a.p.wait(timeout=4), 0)

    def test_all_slots_reusable_with_fresh_ids(self):
        a = self.a
        for sid in range(1, 5):
            a.opened(sid)
        for sid in range(1, 5):
            a.send(CLOSE, sid)
            a.assert_event(EXIT, sid)
        for sid in range(5, 9):
            a.opened(sid)
        a.command(8, "printf 'reused-slot\\n'\n")
        a.output_until(8, b'reused-slot\r\n')

    def test_capacity_fail_closed(self):
        for sid in range(1, 5):
            self.a.opened(sid)
        self.a.send(OPEN, 5, struct.pack('!HH', 80, 24) + b'xterm')
        self.assertEqual(self.a.p.wait(timeout=4), 2)

    def test_reused_session_rejected(self):
        self.a.opened(1)
        self.a.send(CLOSE, 1)
        self.a.assert_event(EXIT, 1)
        self.a.send(OPEN, 1, struct.pack('!HH', 80, 24) + b'xterm')
        self.assertEqual(self.a.p.wait(timeout=4), 2)

    def test_no_credit_does_not_block_other_session(self):
        a = self.a
        a.send(OPEN, 1, struct.pack('!HH', 80, 24) + b'xterm')
        a.assert_event(OPENED, 1)
        a.opened(2)
        a.command(1, "yes stalled\n")
        a.command(2, "printf 'independent\\n'\n")
        a.output_until(2, b'independent\r\n')
        self.assertFalse(any(event[:2] == (OUTPUT, 1) for event in a.deferred))
        a.send(CLOSE, 1)
        a.assert_event(EXIT, 1)

    def test_malformed_and_partial_eof(self):
        self.a.p.stdin.write(struct.pack('!4sBBHII', b'P5GA', 1, INPUT, 0, 1, 4097))
        self.a.p.stdin.flush()
        self.assertEqual(self.a.p.wait(timeout=4), 2)

    def test_transport_eof_reaps_shell_and_child(self):
        a = self.a
        a.opened(1)
        a.command(1, "sleep 120 & printf 'PIDS:%s:%s:END\\n' $$ $!\n")
        out = a.output_until(1, b':END\r\n')
        import re
        match = re.search(rb'PIDS:(\d+):(\d+):END', out)
        self.assertIsNotNone(match)
        pids = [int(pid) for pid in match.groups()]
        a.p.stdin.close()
        self.assertEqual(a.p.wait(timeout=4), 0)
        for pid in pids:
            stat = f'/proc/{pid}/stat'
            if os.path.exists(stat):
                # Orphaned grandchild can await the container's init reaper.
                with open(stat) as state:
                    self.assertEqual(state.read().split()[2], 'Z')


class ExecTests(unittest.TestCase):
    """Exec uses a socket pair: exact bytes, a real end of input, and its status."""

    def exec_payload(self, command, term=b'dumb', cols=80, rows=24):
        return struct.pack('!HHB', cols, rows, len(term)) + term + command

    def run_exec(self, command, data):
        agent = Agent()
        self.addCleanup(agent.p.kill)
        agent.send(EXEC, 1, self.exec_payload(command))
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        if data:
            agent.send(INPUT, 1, data)
        agent.send(INPUT_EOF, 1)
        output = b''
        while True:
            kind, sid, payload = agent.receive(10)
            self.assertEqual(sid, 1)
            if kind == OUTPUT:
                output += payload
                agent.send(CREDIT, 1, struct.pack('!I', len(payload)))
            elif kind == EXIT:
                return output, struct.unpack('!I', payload)[0]

    def test_binary_input_ends_and_status_returns(self):
        data = bytes(range(256)) * 8
        output, status = self.run_exec(b'cat; exit 6', data)
        self.assertEqual((output, status), (data, 6))

    def test_exec_has_what_the_login_profile_would_give(self):
        output, status = self.run_exec(b'printf "%s\\n" "$SSL_CERT_FILE" "$PATH"', b'')
        self.assertEqual(status, 0)
        self.assertIn(b'/etc/ssl/certs/ca-certificates.crt', output)
        self.assertIn(b'/mnt/data/bin:', output)

    def test_resize_of_exec_is_harmless(self):
        agent = Agent()
        self.addCleanup(agent.p.kill)
        agent.send(EXEC, 1, self.exec_payload(b'sleep 0.2; exit 2'))
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(RESIZE, 1, struct.pack('!HH', 100, 40))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        while True:
            kind, _, payload = agent.receive(10)
            if kind == EXIT:
                self.assertEqual(struct.unpack('!I', payload)[0], 2)
                return

    def test_input_to_a_command_that_stopped_reading_is_still_acknowledged(self):
        # The host sends one input frame at a time and waits for INPUT_OK; a command
        # that closed its input must not leave that session waiting forever.
        agent = Agent()
        self.addCleanup(agent.p.kill)
        agent.send(EXEC, 1, self.exec_payload(b'exec 0<&- 1>&- 2>&-; sleep 1; exit 3'))
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        time.sleep(0.4)
        agent.send(INPUT, 1, b'ignored input')
        kinds = []
        while EXIT not in kinds:
            kind, sid, payload = agent.receive(10)
            self.assertEqual(sid, 1)
            kinds.append(kind)
            if kind == INPUT_OK:
                self.assertEqual(payload, struct.pack('!I', len(b'ignored input')))
            if kind == EXIT:
                self.assertEqual(struct.unpack('!I', payload)[0], 3)
        self.assertIn(INPUT_OK, kinds)
        self.assertLess(kinds.index(INPUT_OK), kinds.index(EXIT))

    def test_command_exiting_with_unread_input_ends_only_its_session(self):
        # Linux resets a socket pair closed with unread data (ECONNRESET). That ends
        # this command's session; the agent and its other sessions keep running.
        agent = Agent()
        self.addCleanup(agent.p.kill)
        agent.send(EXEC, 1, self.exec_payload(b'exec 0<&-; sleep 0.5; echo STILL_RUNNING; exit 3'))
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        for _ in range(6):
            agent.send(INPUT, 1, b'x' * 4000)
            agent.assert_event(INPUT_OK, 1, struct.pack('!I', 4000))
        agent.send(INPUT_EOF, 1)
        output = b''
        while True:
            kind, sid, payload = agent.receive(10)
            self.assertEqual(sid, 1)
            if kind == OUTPUT:
                output += payload
                agent.send(CREDIT, 1, struct.pack('!I', len(payload)))
            elif kind == EXIT:
                break
        self.assertIn(b'STILL_RUNNING', output)
        self.assertEqual(struct.unpack('!I', payload)[0], 3)
        # The agent survived: a new session still works.
        agent.send(EXEC, 2, self.exec_payload(b'echo AFTER'))
        self.assertEqual(agent.receive()[:2], (OPENED, 2))
        agent.send(CREDIT, 2, struct.pack('!I', 8192))
        output = b''
        while True:
            kind, sid, payload = agent.receive(10)
            if kind == OUTPUT:
                output += payload
            elif kind == EXIT:
                break
        self.assertEqual((output, struct.unpack('!I', payload)[0]), (b'AFTER\n', 0))

    def collect(self, agent, sid):
        output = b''
        while True:
            kind, got, payload = agent.receive(10)
            self.assertEqual(got, sid)
            if kind == OUTPUT:
                output += payload
                agent.send(CREDIT, sid, struct.pack('!I', len(payload)))
            elif kind == EXIT:
                return output, struct.unpack('!I', payload)[0]

    def test_long_command_runs(self):
        # Warp's SSH bootstrap alone is several KiB.
        agent = Agent()
        self.addCleanup(agent.p.kill)
        command = b': ' + b'x' * 12000 + b'\necho LONG_OK; exit 5'
        agent.send(EXEC, 1, self.exec_payload(command))
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        agent.send(INPUT_EOF, 1)
        self.assertEqual(self.collect(agent, 1), (b'LONG_OK\n', 5))

    def test_command_over_the_limit_is_a_protocol_error(self):
        agent = Agent()
        agent.send(EXEC, 1, self.exec_payload(b'x' * 16385))
        self.assertEqual(agent.p.wait(timeout=5), 2)

    def test_command_on_a_pty_when_the_client_asked_for_one(self):
        # ssh -t host 'command': a real terminal, its size, and resize.
        agent = Agent()
        self.addCleanup(agent.p.kill)
        payload = struct.pack('!HHB', 90, 33, 0x80 | len(b'xterm-256color')) + b'xterm-256color' + \
            b'tty; stty size; echo TERM=$TERM; read line; stty size; exit 4'
        agent.send(EXEC, 1, payload)
        self.assertEqual(agent.receive()[:2], (OPENED, 1))
        agent.send(CREDIT, 1, struct.pack('!I', 8192))
        output = b''
        while b'TERM=xterm-256color' not in output:
            kind, sid, data = agent.receive(10)
            self.assertEqual((kind, sid), (OUTPUT, 1))
            output += data
            agent.send(CREDIT, 1, struct.pack('!I', len(data)))
        agent.send(RESIZE, 1, struct.pack('!HH', 120, 40))
        time.sleep(0.2)
        agent.send(INPUT, 1, b'go\r')
        rest, status = self.collect(agent, 1)
        output += rest
        self.assertIn(b'/dev/pts/', output)
        self.assertIn(b'33 90', output)
        self.assertIn(b'40 120', output)
        self.assertEqual(status, 4)

    def test_nul_in_command_is_rejected(self):
        agent = Agent()
        agent.send(EXEC, 1, self.exec_payload(b'true\0rm'))
        self.assertEqual(agent.p.wait(timeout=5), 2)


if __name__ == '__main__':
    unittest.main()
