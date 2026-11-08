"""Hardware development is explicitly restricted to the phat console.
SPDX-License-Identifier: GPL-3.0-or-later
"""
PHAT_ADDR = '192.168.86.99'


def require_phat(addr: str) -> str:
    if addr != PHAT_ADDR:
        raise ValueError('Explicit phat address 192.168.86.99 required; no discovery or fallback')
    return addr
