#!/usr/bin/env python3
"""Read-only export metadata for decrypted ELF64 modules; never proves runtime support.

Supports the sectionless, standard-DT-pointer layout in supplied 13.60 modules.
Unsupported layouts fail explicitly. No module bytes are copied to the report.
GPL-3.0-or-later.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct

DEFAULT_NAMES = ('fork', 'vfork', 'sceKernelSpawn', 'rfork_thread', 'execve',
                 'waitpid', 'posix_openpt', 'grantpt', 'unlockpt', 'ptsname',
                 'setsid', 'setpgid', 'ioctl', 'kill', 'dup2')


def nid(name):
    # Same algorithm as tooling/native/sce_module_writer.cpp::nid.
    suffix = bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')
    digest = hashlib.sha1(name.encode('ascii') + suffix).digest()
    return base64.b64encode(digest[:8][::-1]).decode().rstrip('=').replace('/', '-')


def audit(data, names=DEFAULT_NAMES):
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('truncated ELF structure')
        return struct.unpack_from(fmt, data, offset)

    header = unpack('<16sHHIQQQIHHHHHH', 0)
    if header[0][:7] != b'\x7fELF\x02\x01\x01' or header[2] != 62:
        raise ValueError('expected little-endian x86-64 ELF')
    if header[8] != 64 or header[9] != 56 or not 1 <= header[10] <= 128:
        raise ValueError('unsupported program header layout')
    programs = [unpack('<IIQQQQQQ', header[5] + i*56) for i in range(header[10])]
    for p in programs:
        if p[2] > len(data) or p[5] > len(data)-p[2]:
            raise ValueError('segment outside file')

    def offset(address, size):
        matches = [p[2]+address-p[3] for p in programs if p[0] == 1 and
                   p[3] <= address and size <= p[5] and address-p[3] <= p[5]-size]
        if len(matches) != 1:
            raise ValueError('table not uniquely file-backed by PT_LOAD')
        return matches[0]

    dynamic = [p for p in programs if p[0] == 2]
    if len(dynamic) != 1 or dynamic[0][5] % 16:
        raise ValueError('expected one aligned PT_DYNAMIC')
    p = dynamic[0]; tags = {}
    for where in range(p[2], p[2]+p[5], 16):
        tag, value = unpack('<qQ', where)
        if tag == 0: break
        if tag in (5, 6, 10, 11, 0x6100003f):
            if tag in tags: raise ValueError('duplicate required dynamic tag')
            tags[tag] = value
    if set(tags) != {5, 6, 10, 11, 0x6100003f} or tags[11] != 24 or tags[0x6100003f] % 24:
        raise ValueError('unsupported or incomplete symbol table layout')
    if tags[10] > 16*1024*1024 or tags[0x6100003f] > 24*1000000:
        raise ValueError('table exceeds audit bound')
    strings = offset(tags[5], tags[10]); symbols = offset(tags[6], tags[0x6100003f])
    wanted = {nid(name): name for name in names}; exports = {name: [] for name in names}
    for where in range(symbols, symbols+tags[0x6100003f], 24):
        no, info, other, section, value, size = unpack('<IBBHQQ', where)
        if no >= tags[10]: raise ValueError('symbol string outside table')
        end = data.find(b'\0', strings+no, strings+tags[10])
        if end < 0: raise ValueError('unterminated symbol string')
        try: encoded = data[strings+no:end].decode('ascii')
        except UnicodeDecodeError as exc: raise ValueError('non-ASCII symbol') from exc
        name = wanted.get(encoded.split('#')[0])
        if name and section and (info >> 4) in (1, 2) and (other & 3) in (0, 3):
            exports[name].append(dict(symbol=encoded, value=value, size=size))
    return dict(schema=1, sha256=hashlib.sha256(data).hexdigest(),
                runtime_support_proven=False, exports=exports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', type=Path)
    args = parser.parse_args()
    if args.module.stat().st_size > 64*1024*1024:
        parser.error('module exceeds 64 MiB audit bound')
    try: result = audit(args.module.read_bytes())
    except ValueError as exc: parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
