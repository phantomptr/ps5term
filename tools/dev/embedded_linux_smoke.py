"""Run one bounded local reference Linux guest; no console or network access.
GPL-3.0-or-later.
"""
from __future__ import annotations
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import pty
import resource
import re
import selectors
import signal
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
# Expected markers do not appear verbatim in the input command's terminal echo.
SCRIPT=r'''printf '\nPS5TERM_%s\n' BEGIN; uname -srm; if [ "$(uname -s)" = Linux ]; then printf 'PS5TERM_%s\n' LINUX; fi; f() { printf shell; }; if [ "$(f)" = shell ]; then printf 'PS5TERM_%s\n' FUNCTION; fi; printf 'beta\nalpha\n' | sort > /tmp/ps5term-probe; if [ "$(head -n 1 /tmp/ps5term-probe)" = alpha ]; then printf 'PS5TERM_%s\n' PIPELINE; fi; sleep 0 & child=$!; if wait "$child"; then printf 'PS5TERM_%s\n' WAIT; fi; mkdir -p /dev/pts; mount -t devpts devpts /dev/pts; busybox script -q -c 'stty size; stty rows 31 cols 97; stty size' /tmp/ps5term-pty; if grep -q '31 97' /tmp/ps5term-pty; then printf 'PS5TERM_%s\n' PTY; fi; rm -f /tmp/ps5term-probe /tmp/ps5term-pty; printf 'PS5TERM_%s\n' DONE
'''

def console_markers(data: bytes):
    # Match entire output lines, never an echoed shell command.
    normalized=re.sub(rb'\r+\n',b'\n',data)
    return {line[len(b'PS5TERM_'):].decode('ascii') for line in normalized.split(b'\n')
            if re.fullmatch(rb'PS5TERM_[A-Z]+',line)}

def run(output: Path, evidence: Path, deadline_seconds=60):
    if not 1<=deadline_seconds<=120: raise ValueError('Guest deadline must be 1-120 seconds')
    binary=output/'source/tinyemu-2019-12-21/temu'; config=output/'guest.cfg'
    from tools.dev.embedded_linux import guest_config
    cfg=json.loads(config.read_text())
    expected=guest_config(output/'images/diskimage-linux-riscv-2018-09-23')
    if cfg!=expected: raise ValueError('Only the bounded reference config is supported')
    image=Path(cfg['drive0']['file']); image_hash=hashlib.sha256(image.read_bytes()).hexdigest()
    master,slave=pty.openpty(); transcript=bytearray(); started=time.monotonic(); sent=False;login=False;done=False;boot=None
    process=None
    try:
        process=subprocess.Popen([str(binary),str(config)],stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave);slave=-1;os.set_blocking(master,False)
        with selectors.DefaultSelector() as selector:
            selector.register(master,selectors.EVENT_READ)
            while time.monotonic()-started<deadline_seconds:
                for _,_ in selector.select(.05):
                    try:data=os.read(master,65536)
                    except OSError as error:
                        if error.errno==errno.EIO:data=b''
                        else:raise
                    if data:
                        if len(transcript)+len(data)>2*1024*1024:raise RuntimeError('Guest transcript limit exceeded')
                        transcript.extend(data)
                tail=bytes(transcript[-16384:])
                if not login and b'login:' in tail:
                    os.write(master,b'root\n');login=True
                if not sent and (b'# ' in tail or b'~]#' in tail):
                    boot=time.monotonic()-started;os.write(master,SCRIPT.encode());sent=True
                if sent and 'DONE' in console_markers(tail):
                    done=True;break
                if process.poll() is not None:break
    finally:
        if process is not None:
            if process.poll() is None:
                os.killpg(process.pid,signal.SIGTERM)
                try:process.wait(timeout=2)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=2)
            else:process.wait(timeout=2)
        os.close(master)
        if slave>=0:os.close(slave)
    unchanged=hashlib.sha256(image.read_bytes()).hexdigest()==image_hash
    text=transcript.decode(errors='replace')
    markers=console_markers(bytes(transcript))
    checks={name:name in markers for name in ('LINUX','FUNCTION','PIPELINE','WAIT','PTY')}
    report=dict(schema=1,host=sys.platform,host_only=True,ps5_runtime_verified=False,ram_mib=128,network=False,host_share=False,
                completed=done,checks=checks,boot_seconds=boot,elapsed_seconds=round(time.monotonic()-started,3),
                child_peak_rss_mib=round(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss/(1024**2 if sys.platform=='darwin' else 1024),2),
                image_unchanged=unchanged,guest_process_reaped=process is not None and process.returncode is not None,
                emulator_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),
                sample_image='Linux RISC-V upstream 2018 sample; research only')
    evidence.parent.mkdir(parents=True,exist_ok=True)
    evidence.with_suffix('.txt').write_text(text)
    evidence.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return done and all(checks.values()) and unchanged and report['guest_process_reaped']

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build'/('embedded-linux-'+sys.platform))
    parser.add_argument('--evidence',type=Path,default=ROOT/'.local/dev/embedded-linux-smoke.json')
    args=parser.parse_args()
    sys.exit(0 if run(args.output.resolve(),args.evidence.resolve()) else 1)
