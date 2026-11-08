#!/usr/bin/env python3
"""Explicit, research-only executable capability metadata. GPL-3.0-or-later.

Baseline: EchoStretch/kstuff-lite ps5-kstuff/uelf/fself.c,
commit 48cd2c4ce62aa9ca0c2cc014c1e512f276ae7c17, s_auth_info_for_exec.
This is public loader-format data, not a private credential or Sony signature.
The research profile retains that loader's default authority/attributes and adds
only the capability tested by syscall 251 in the archived 13.60 kernel.
No live kernel offsets, credential writes, root identity, or all-caps profile.
"""
import argparse
import json
import struct

BASELINE_WORDS=(
    0x4400001084c2052d, 0x2000038000000000, 0x000000000000ff00, 0, 0,
    0x4000400040000000, 0x4000000000000000, 0x0080000000000002,
    0xf0000000ffff4000, 0, 0, 0, 0, 0, 0, 0, 0,
)
MARKER='PS5TERM_RFORK_AUTH_RESEARCH'

def select_profile(profile, definitions):
    if any(item.split('=')[0]==MARKER for item in definitions):
        raise ValueError('Research auth marker is generated only by the explicit profile')
    if profile=='default':
        return {}
    if profile!='rfork-research':
        raise ValueError('Unknown executable auth profile')
    child_definitions=[item for item in definitions if item.split('=')[0]=='PS5TERM_CHILD_RESEARCH']
    if not child_definitions or any(item not in ('PS5TERM_CHILD_RESEARCH','PS5TERM_CHILD_RESEARCH=1')
                                    for item in child_definitions):
        raise ValueError('rfork-research requires the separately opted-in child experiment')
    words=list(BASELINE_WORDS)
    words[1]|=1<<62
    return dict(schema=1,definition=MARKER,authority=hex(words[0]),words=words,
                auth_info=struct.pack('<17Q',*words).hex())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',default='default')
    parser.add_argument('--definition',action='append',default=[])
    args=parser.parse_args()
    try: result=select_profile(args.profile,args.definition)
    except ValueError as exc: parser.error(str(exc))
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':main()
