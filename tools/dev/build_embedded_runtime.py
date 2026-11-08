"""Build/test the isolated in-process host adapter. No PS5 deployment.
GPL-3.0-or-later. Sources/images remain pinned research material.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tools.dev.embedded_linux import ROOT, ARCHIVES, prepare, extract_checked

def build(output: Path, sanitize=False, native_objects=False, host_arch=None, thread_sanitize=False, mapped_buffers=False):
    if sanitize and thread_sanitize: raise ValueError('Address and thread sanitizers require separate builds')
    if native_objects and (sanitize or thread_sanitize): raise ValueError('Native objects are compile-only; use host sanitizers')
    if host_arch and (sys.platform!='darwin' or native_objects): raise ValueError('Explicit host architecture is macOS-only')
    if output.exists(): raise FileExistsError('Choose a fresh output directory')
    cache=prepare()
    extract_checked(cache/'tinyemu-2019-12-21.tar.gz',output/'source')
    source=output/'source/tinyemu-2019-12-21'
    patches=[ROOT/'tooling/subsystem'/name for name in ('tinyemu-host-profile.patch','tinyemu-embedded-profile.patch')]
    for patch in patches:
        subprocess.run(['patch','-p1','--fuzz=0','-i',str(patch)],cwd=source,check=True)
    adapter=ROOT/'backend/subsystem'
    cc=os.environ.get('HOST_CC','clang')
    compiler=[cc]
    compiler_environment=dict(os.environ)
    if native_objects:
        compiler=['sh',str(ROOT/'tooling/prospero-clang18')]
        compiler_environment['PS5_PAYLOAD_SDK']=str(ROOT/'.deps/native/ps5-payload-sdk')
    flags=['-std=gnu11','-O1' if sanitize or thread_sanitize else '-O2','-g','-fwrapv','-D_GNU_SOURCE','-D_FILE_OFFSET_BITS=64',
           '-DCONFIG_RISCV_MAX_XLEN=64','-DPS5TERM_TINYEMU_BUILTIN_BSWAP','-I'+str(source),'-I'+str(adapter)]
    if mapped_buffers: flags+=['-DPS5TERM_GUEST_MMAP_TEST']
    if not native_objects: flags+=['-DPS5TERM_SUBSYSTEM_TESTING']
    if not native_objects: flags+=['-pthread']
    if host_arch: flags+=['-arch',host_arch]
    if sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if thread_sanitize: flags+=['-fsanitize=thread','-fno-omit-frame-pointer']
    specs=[(source/name,[]) for name in ('virtio.c','pci.c','fs.c','cutils.c','iomem.c','simplefb.c','softfp.c','riscv_machine.c')]
    specs += [(source/'riscv_cpu.c',['-DMAX_XLEN='+str(width)]) for width in (32,64)]
    commands=[]
    for index,(path,extra) in enumerate(specs):
        # Pinned interpreter implements sign extension with signed left shifts.
        # UBSan reproduced that upstream idiom in sext(). Keep the adapter fully
        # instrumented, and exclude shift-base only for the engine objects.
        engine_flags=['-fno-sanitize=shift-base'] if sanitize else []
        commands.append([*compiler,*flags,*engine_flags,'-DPS5TERM_EMBEDDED','-include',str(adapter/'tinyemu_hooks.h'),*extra,
                         '-c',str(path),'-o',str(output/('engine-'+str(index)+'.o'))])
    owned=[(adapter/'embedded_linux.c','adapter.o'),(adapter/'linux_session.c','session.o')]
    if not native_objects:
        owned += [(ROOT/'tests/subsystem/test_embedded_runtime.c','acceptance.o'),
                  (ROOT/'tests/subsystem/test_linux_session.c','session-acceptance.o'),
                  (ROOT/'tests/subsystem/test_shared_9p.c','share-acceptance.o'),
                  (ROOT/'tests/subsystem/test_network_bridge.c','network-acceptance.o')]
    commands += [[*compiler,*flags,'-Wall','-Wextra','-Werror','-c',str(path),'-o',str(output/name)] for path,name in owned]
    def compile_one(item):
        index,command=item
        with (output/('compile-'+str(index)+'.log')).open('w') as log:
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120,env=compiler_environment)
    with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(compile_one,enumerate(commands)))
    binary=output/'test-embedded-runtime'
    objects=[str(output/('engine-'+str(i)+'.o')) for i in range(len(specs))]
    if native_objects:
        # A relocatable object exposes remaining imports, without producing a
        # payload/app, linking private system modules or touching a console.
        binary=output/'embedded-linux-core.o'
        subprocess.run(['ld.lld-18','-r',*objects,str(output/'adapter.o'),str(output/'session.o'),'-o',str(binary)],check=True,timeout=60)
        imports=subprocess.check_output(['llvm-nm-18','--undefined-only',str(binary)],text=True)
        (output/'imports.txt').write_text(imports)
        sdk=Path(compiler_environment['PS5_PAYLOAD_SDK'])
        subprocess.run(['ld.lld-18','-shared','-z','defs',str(binary),
                        str(sdk/'target/lib/libpthread.a'),
                        str(sdk/'target/lib/libSceLibcInternal.so'),str(sdk/'target/lib/libkernel.so'),
                        '-o',str(output/'core-link-check.so')],check=True,timeout=60)
        (output/'provenance.json').write_text(json.dumps(dict(compile_only=True,ps5_runtime_verified=False,
            archives=ARCHIVES,patches={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches},
            compile_commands=commands,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),indent=2)+'\n')
        print('Compile/link-only PS5 core:',binary);print(imports);return
    subprocess.run([cc,*flags,*objects,str(output/'adapter.o'),str(output/'acceptance.o'),'-o',str(binary)],check=True,timeout=60)
    session_binary=output/'test-linux-session'
    subprocess.run([cc,*flags,*objects,str(output/'adapter.o'),str(output/'session.o'),
                    str(output/'session-acceptance.o'),'-o',str(session_binary)],check=True,timeout=60)
    share_binary=output/'test-shared-9p'
    subprocess.run([cc,*flags,str(output/'share-acceptance.o'),'-o',str(share_binary)],check=True,timeout=60)
    subprocess.run([str(share_binary)],check=True,timeout=15)
    extract_checked(cache/'diskimage-linux-riscv-2018-09-23.tar.gz',output/'images')
    network_binary=output/'test-network-bridge'
    subprocess.run([cc,*flags,*objects,str(output/'adapter.o'),str(output/'network-acceptance.o'),'-o',str(network_binary)],check=True,timeout=60)
    environment=dict(os.environ,TSAN_OPTIONS='halt_on_error=1',ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')+':halt_on_error=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(network_binary),str(output/'images/diskimage-linux-riscv-2018-09-23')],check=True,timeout=30,env=environment)
    with (output/'acceptance.log').open('w') as log:
        subprocess.run([str(binary),str(output/'images/diskimage-linux-riscv-2018-09-23')],
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120,env=environment)
    with (output/'session-acceptance.log').open('w') as log:
        subprocess.run([str(session_binary),str(output/'images/diskimage-linux-riscv-2018-09-23')],
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120,env=environment)
    (output/'provenance.json').write_text(json.dumps(dict(host_only=True,ps5_runtime_verified=False,archives=ARCHIVES,
        adapter_sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [adapter/'embedded_linux.c',adapter/'embedded_linux.h',adapter/'tinyemu_hooks.h',adapter/'guest_memory.h',adapter/'guest_trace.h',
                                   adapter/'linux_session.c',adapter/'linux_session.h',adapter/'shared_folder.h',adapter/'shared_9p.inc',
                                   ROOT/'tests/subsystem/test_embedded_runtime.c',ROOT/'tests/subsystem/test_linux_session.c',ROOT/'tests/subsystem/test_shared_9p.c',ROOT/'tests/subsystem/test_network_bridge.c']},
        sanitizer=sanitize,thread_sanitizer=thread_sanitize,patches={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches},
        compile_commands=commands,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        session_binary_sha256=hashlib.sha256(session_binary.read_bytes()).hexdigest()),indent=2)+'\n')
    print((output/'acceptance.log').read_text())
    print((output/'session-acceptance.log').read_text())

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mapped-buffers',action='store_true',help='Exercise native-style mapped guest buffers on the host')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--thread-sanitize',action='store_true',help='Separate host ThreadSanitizer build')
    parser.add_argument('--native-objects',action='store_true',help='Linux Clang18 + cached PS5 SDK; compile-only, never deploys')
    parser.add_argument('--host-arch',choices=['arm64','x86_64'],help='macOS reference architecture; x86_64 requires installed Rosetta on Apple silicon')
    parser.add_argument('--output',type=Path,default=ROOT/'build'/('embedded-runtime-'+sys.platform))
    args=parser.parse_args();build(args.output.resolve(),args.sanitize,args.native_objects,args.host_arch,args.thread_sanitize,args.mapped_buffers)
