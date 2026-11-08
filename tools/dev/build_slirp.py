# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the isolated bounded libslirp profile. No console mutation or app enablement."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tools.dev.prepare_slirp import prepare, SHA256, COMMIT

ROOT=Path(__file__).resolve().parents[2]

def build(output: Path, native=False):
    output=output.resolve()
    if output.exists():
        raise ValueError("choose a fresh output directory")
    source=prepare(ROOT/"third_party/libslirp/libslirp-4.9.7.tar.gz",output/"source")/"src"
    owned=ROOT/"backend/subsystem/net"
    compiler=[os.environ.get("HOST_CC","clang")]
    flags=["-std=gnu11","-O1","-g","-pthread","-I"+str(owned),"-I"+str(source)]
    env=dict(os.environ)
    if native:
        compiler=["sh",str(ROOT/"tooling/prospero-clang18")]
        env["PS5_PAYLOAD_SDK"]=str(ROOT/".deps/native/ps5-payload-sdk")
        flags += ["-DPS5TERM_NATIVE_NET"]
    else:
        flags += ["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
    sources=[*sorted(source.glob("*.c")),*sorted(owned.glob("*.c"))]
    if native:sources.append(ROOT/"backend/owned_ssh/native_entropy.c")
    commands=[]
    objects=[]
    for i,path in enumerate(sources):
        obj=output/f"object-{i}.o";objects.append(str(obj))
        command=[*compiler,*flags,"-c",str(path),"-o",str(obj)];commands.append(command)
        subprocess.run(command,check=True,timeout=60,env=env)
    if native:
        binary=output/"network-core.o"
        subprocess.run(["ld.lld-18","-r",*objects,"-o",str(binary)],check=True)
        imports=subprocess.check_output(["llvm-nm-18","--undefined-only",str(binary)],text=True)
        (output/"imports.txt").write_text(imports)
        sdk=Path(env["PS5_PAYLOAD_SDK"])/"target/lib"
        subprocess.run(["ld.lld-18","-shared","-z","defs",str(binary),
                        str(sdk/"libpthread.a"),str(sdk/"libSceLibcInternal.so"),
                        str(sdk/"libkernel.so"),"-o",str(output/"network-link-check.so")],check=True)
    else:
        for name in ("resources","socket_policy","nat"):
            binary=output/f"test-{name}"
            subprocess.run([*compiler,*flags,"-Wall","-Wextra","-Werror",
                            "-I"+str(ROOT/"backend/subsystem"),str(ROOT/f"tests/subsystem/test_network_{name}.c"),
                            *objects,"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=30)
    files=[*sources,*owned.glob("*.h"),ROOT/"tools/dev/prepare_slirp.py",Path(__file__)]
    (output/"provenance.json").write_text(json.dumps({
        "archive_sha256":SHA256,"commit":COMMIT,"native_compile_only":native,
        "ps5_runtime_verified":False,"commands":commands,
        "sources":{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    },indent=2)+"\n")
    return objects

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path);parser.add_argument("--native-objects",action="store_true")
    args=parser.parse_args();build(args.output,args.native_objects)
