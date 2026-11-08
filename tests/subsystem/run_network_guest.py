#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-only libslirp acceptance with localhost HTTP/DNS; no Internet fixture."""
import argparse
import http.server
import os
from pathlib import Path
import socket
import sys
import struct
import subprocess
import threading


def run(binary: Path, images: Path):
    counts = {'http': 0, 'dns': 0}
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != '/proof':
                self.send_error(404)
                return
            body = b'ps5term-network-proof\n'
            counts['http'] += 1
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *_):
            pass
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    dns = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dns.bind(('127.0.0.1', 0))
    dns.settimeout(.1)
    stop = threading.Event()
    errors = []
    def serve_dns():
        while not stop.is_set():
            try:
                packet, addr = dns.recvfrom(512)
            except socket.timeout:
                continue
            try:
                if len(packet) < 12 or packet[4:6] != b'\0\1':
                    continue
                pos, labels = 12, []
                while pos < len(packet) and packet[pos]:
                    size = packet[pos]
                    if size > 63 or pos + size + 1 >= len(packet):
                        raise ValueError('Malformed DNS fixture question')
                    labels.append(packet[pos+1:pos+1+size])
                    pos += 1 + size
                question_end = pos + 5
                if b'.'.join(labels) != b'ps5term.test' or packet[pos+1:question_end] != b'\0\1\0\1':
                    continue
                counts['dns'] += 1
                answer = b'\xc0\x0c' + struct.pack('!HHIH', 1, 1, 30, 4) + bytes([203, 0, 113, 7])
                reply = packet[:2] + b'\x81\x80\0\1\0\1\0\0\0\0' + packet[12:question_end] + answer
                dns.sendto(reply, addr)
            except Exception as error:
                errors.append(error)
                stop.set()
    threads = [threading.Thread(target=httpd.serve_forever), threading.Thread(target=serve_dns)]
    for thread in threads:
        thread.start()
    try:
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')+':halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        subprocess.run([str(binary.resolve()), str(images.resolve()), str(httpd.server_port), str(dns.getsockname()[1])], check=True, timeout=90, env=env)
        assert not errors, errors
        assert counts['http'] == 2 and counts['dns'] >= 2, counts
        print('PASS: localhost fixture requests:', counts)
    finally:
        stop.set()
        httpd.shutdown()
        for thread in threads:
            thread.join(timeout=3)
            assert not thread.is_alive()
        httpd.server_close()
        dns.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('images', type=Path)
    args = parser.parse_args()
    run(args.binary, args.images)
