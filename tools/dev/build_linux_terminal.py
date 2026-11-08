"""Test the experimental Linux/VT adapter using a verified host runtime build.
GPL-3.0-or-later. Does not build or deploy a PS5 app.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
from tools.dev.embedded_linux import ROOT

def build(runtime: Path, output: Path):
    manifest=json.loads((runtime/'provenance.json').read_text())
    if not manifest.get('host_only'): raise ValueError('A verified host runtime is required')
    for file,digest in manifest['adapter_sources'].items():
        if hashlib.sha256((ROOT/file).read_bytes()).hexdigest()!=digest: raise ValueError('Stale runtime source: '+file)
    for file,digest in manifest['patches'].items():
        if hashlib.sha256((ROOT/'tooling/subsystem'/file).read_bytes()).hexdigest()!=digest: raise ValueError('Stale runtime patch: '+file)
    if output.exists(): raise FileExistsError('Choose a fresh output directory')
    subprocess.run([sys.executable,str(ROOT/'tools/verify-vterm.py')],check=True)
    output.mkdir(parents=True)
    cc=os.environ.get('HOST_CC','clang');cxx=os.environ.get('HOST_CXX','clang++')
    flags=['-O1','-g','-fwrapv','-pthread','-fno-omit-frame-pointer','-I'+str(ROOT/'src'),
           '-I'+str(ROOT/'backend/subsystem'),'-I'+str(ROOT/'third_party/libvterm/include')]
    if manifest.get('sanitizer'):flags+=['-fsanitize=address,undefined']
    if manifest.get('thread_sanitizer'):flags+=['-fsanitize=thread']
    first=manifest['compile_commands'][0]
    if '-arch' in first:flags+=['-arch',first[first.index('-arch')+1]]
    sources=[*sorted((ROOT/'third_party/libvterm/src').glob('*.c')),ROOT/'src/terminal_memory.c',
             ROOT/'src/terminal_model.cpp',ROOT/'src/scrollback.cpp',ROOT/'backend/subsystem/linux_terminal.cpp',
             ROOT/'tests/subsystem/test_linux_terminal.cpp']
    objects=[];commands=[]
    for index,source in enumerate(sources):
        obj=output/(str(index)+'.o');objects.append(str(obj))
        warnings=[] if ROOT/'third_party' in source.parents else ['-Wall','-Wextra','-Werror']
        command=[cc if source.suffix=='.c' else cxx,'-std=c11' if source.suffix=='.c' else '-std=c++20',
                 *flags,*warnings,'-c',str(source),'-o',str(obj)]
        commands.append(command)
        subprocess.run(command,check=True,timeout=60)
    engine=[*sorted(runtime.glob('engine-*.o')),runtime/'adapter.o',runtime/'session.o']
    binary=output/'test-linux-terminal'
    subprocess.run([cxx,*flags,*map(str,engine),*objects,'-o',str(binary)],check=True,timeout=60)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')+':halt_on_error=1',
             UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',TSAN_OPTIONS='halt_on_error=1')
    with (output/'acceptance.log').open('w') as log:
        subprocess.run([str(binary),str(runtime/'images/diskimage-linux-riscv-2018-09-23')],
                       check=True,timeout=120,env=env,stdout=log,stderr=subprocess.STDOUT)
    (output/'provenance.json').write_text(json.dumps(dict(host_only=True,ps5_runtime_verified=False,
        runtime=str(runtime),runtime_manifest_sha256=hashlib.sha256((runtime/'provenance.json').read_bytes()).hexdigest(),
        sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        commands=commands,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),indent=2)+'\n')
    print((output/'acceptance.log').read_text())
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();build(args.runtime.resolve(),args.output.resolve())
