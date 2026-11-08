#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare the pinned, private libslirp profile; never mutate upstream cache."""
from pathlib import Path
import argparse
import hashlib
import json
import tarfile

SHA256 = "6b19073be7764feabfaa55543b4567577b1a004dae7bbfc405a384f36fedaf3c"
COMMIT = "ce12dcddf710a71dc0de6450ec95edbadf8e9f66"

ARCHIVE = Path(__file__).resolve().parents[2]/"third_party/libslirp/libslirp-4.9.7.tar.gz"

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"upstream profile anchor changed: {old[:60]!r}")
    return text.replace(old, new, 1)

def replace_exactly(text, old, new, count):
    if text.count(old) != count:
        raise ValueError(f"upstream profile anchor changed: {old[:60]!r}")
    return text.replace(old, new)

def profile_files(archive: Path):
    """Return the private profile as {relative name: bytes}; writes nothing."""
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise ValueError("libslirp archive hash mismatch")
    files = {}
    with tarfile.open(archive, "r:gz") as tar:
        for m in tar.getmembers():
            if m.name in ("COPYRIGHT", "LICENSE", "meson.build") or (m.name.startswith("src/") and m.name.endswith((".h", ".c", ".in"))):
                if not m.isfile() or ".." in Path(m.name).parts or m.size > 1024*1024:
                    raise ValueError("unsafe source member")
                files[m.name] = tar.extractfile(m).read()
    def edit(name, transform):
        files[name] = transform(files[name].decode()).encode()
    def misc(text):
        start=text.index("static int slirp_socketpair_with_oob(")
        end=text.index("SLIRP_EXPORT\nchar *slirp_connection_info", start)
        # Remove process creation and Unix forwarding, including their GLib deps.
        return text[:start]+'''/* PS5Term profile: no subprocess or Unix-domain forwarding. */
int fork_exec(struct socket *so, const char *command)
{ (void)so; (void)command; return 0; }
int open_unix(struct socket *so, const char *path)
{ (void)so; (void)path; return 0; }

'''+text[end:]
    edit("src/misc.c", misc)
    def no_tftp_dns(text):
        start=text.index("        if (slirp->tftp_server_name) {")
        end=start+text[start:].index("{")+1
        depth=1
        while depth:
            if text[end]=="{":depth+=1
            elif text[end]=="}":depth-=1
            end+=1
        return text[:start]+"        /* PS5Term: no TFTP server or host resolver. */"+text[end:]
    edit("src/bootp.c", no_tftp_dns)
    # Assertions in the library must fail its C guard, not abort the app.
    edit("src/util.h", lambda s: replace_once(s, '#include "libslirp.h"', '#include "libslirp.h"\n#undef assert\n#define assert(x) g_assert(x)'))
    edit("src/util.h", lambda s: replace_once(s, '#define closesocket(s) close(s)', '#define closesocket(s) ps5_net_socket_close(s)'))
    edit("src/util.h", lambda s: replace_once(s, '#include "libslirp.h"', '#include "libslirp.h"\n#include "socket_policy.h"'))
    def sockets(text):
        start=text.index("void slirp_set_nonblock(")
        end=text.index("#if defined(_WIN32)\nstatic int win32_socket_error", start)
        return text[:start]+'''void slirp_set_nonblock(slirp_os_socket fd)
{ ps5_net_socket_nonblock(fd); }
slirp_os_socket slirp_socket(int domain, int type, int protocol)
{ return ps5_net_socket(domain, type, protocol); }

'''+text[end:]
    edit("src/util.c", sockets)
    def resolver(text):
        start=text.index("#if defined(_WIN32)", text.index("#define TIMEOUT_DEFAULT"))
        end=text.index("static void slirp_init_once(void)",start)
        return text[:start]+'''/* PS5Term: explicit resolver, never read the host filesystem. */
int get_dns_addr(struct in_addr *address, uint16_t *port)
{ return ps5_net_resolver(address, port); }
int get_dns6_addr(struct in6_addr *address, uint16_t *port, uint32_t *scope)
{ (void)address; (void)port; (void)scope; return -1; }

'''+text[end:]
    edit("src/slirp.c", resolver)
    # Upstream translates the source IP before checking custom resolver ports.
    # Restore the virtual DNS port for either advertised guest DNS endpoint.
    edit("src/socket.c", lambda s: replace_once(s,
        "so->so_faddr.s_addr == slirp->vhost_addr.s_addr &&\n            so->so_fport == htons(53)",
        "(so->so_faddr.s_addr == slirp->vhost_addr.s_addr ||\n             so->so_faddr.s_addr == slirp->vnameserver_addr.s_addr) &&\n            so->so_fport == htons(53)"))
    edit("src/socket.c", lambda s: replace_once(s,
        "sin->sin_addr = slirp->vhost_addr;\n            sin->sin_port = htons(53);",
        "sin->sin_addr = so->so_faddr;\n            sin->sin_port = htons(53);"))
    # qlink overlays IPv4 packet storage aligned to four bytes; pointer fields
    # need explicit packed accesses on 64-bit hosts (UBSan reproduced this).
    edit("src/ip.h", lambda s: replace_once(s,
        "struct qlink {\n    void *next, *prev;\n};",
        "SLIRP_PACKED_BEGIN\nstruct qlink {\n    void *next, *prev;\n} SLIRP_PACKED_END;"))
    edit("src/misc.h", lambda s: replace_once(s,
        "struct slirp_quehead {\n    struct slirp_quehead *qh_link;\n    struct slirp_quehead *qh_rlink;\n};",
        "SLIRP_PACKED_BEGIN\nstruct slirp_quehead {\n    struct slirp_quehead *qh_link;\n    struct slirp_quehead *qh_rlink;\n} SLIRP_PACKED_END;"))
    # if_encap keeps one Ethernet frame on the stack, sized by this limit. The
    # upstream 65521 byte maximum overflowed the PS5 worker stack on the first
    # DHCP reply. The profile's fixed MTU is 1500, matching the engine's frames.
    edit("src/if.h", lambda s: replace_once(s, "#define IF_MTU_MAX 65521", "#define IF_MTU_MAX 1500"))
    # No TFTP service: this removes the only guest-reachable host file paths.
    files["src/tftp.c"]=b'''/* PS5Term profile: no TFTP server, no host file access from the guest. */
#include "slirp.h"
#include "tftp.h"
void tftp_cleanup(Slirp *slirp) { (void)slirp; }
void tftp_input(struct sockaddr_storage *srcsas, struct mbuf *m) { (void)srcsas; (void)m; }
'''
    # Fuzzing-trace sequence alignment reads the process environment; the app
    # never provides one and must not import getenv for the packet path.
    edit("src/tcp_input.c", lambda s: replace_exactly(s, 'if (getenv("SLIRP_FUZZING") &&', 'if (((const char *)0) &&', 2))
    # FIONREAD only sizes a UDP receive buffer. The owned policy supplies a
    # bounded default when the native socket layer rejects the request.
    edit("src/util.h", lambda s: replace_once(s, '#define ioctlsocket(s, r, v) ioctl(s, r, v)', '#define ioctlsocket(s, r, v) ps5_net_ioctl(s, r, v)'))
    version=files["src/libslirp-version.h.in"].decode()
    for key,value in {"MAJOR_VERSION":"4", "MINOR_VERSION":"9", "MICRO_VERSION":"7", "VERSION_STRING":'"4.9.7-ps5term"'}.items():
        version=version.replace(f"@SLIRP_{key}@", value)
    files["src/libslirp-version.h"]=version.encode()
    files["ps5term-provenance.json"]=(json.dumps({"archive_sha256":SHA256,"commit":COMMIT,"profile":"private compatibility subset: no process spawn, Unix forwarding, TFTP, host resolver discovery or IPv6 DNS; explicit resolver and owned socket policy"},indent=2)+"\n").encode()
    return files

def prepare(archive: Path, output: Path):
    if output.exists():
        raise ValueError("choose a new output directory; existing outputs are preserved")
    files = profile_files(archive)
    output.mkdir(parents=True)
    for name,data in files.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    return output

def prepared_source(root: Path):
    """Immutable cache under .deps keyed by archive and profile tool; every use
    re-derives the profile and refuses a drifted cache instead of using it."""
    import errno
    import tempfile
    archive = root/"third_party/libslirp/libslirp-4.9.7.tar.gz"
    files = profile_files(archive)
    key = hashlib.sha256(json.dumps({"archive":SHA256,"tool":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},sort_keys=True).encode()).hexdigest()
    cache = root/".deps/subsystem/prepared-slirp"; cache.mkdir(parents=True, exist_ok=True)
    target = cache/key
    if not target.exists():
        with tempfile.TemporaryDirectory(prefix="preparing-", dir=cache) as tmp:
            staging = Path(tmp)/"prepared"
            prepare(archive, staging)
            try: staging.rename(target)
            except OSError as error:
                if error.errno not in (errno.EEXIST, errno.ENOTEMPTY): raise
    if target.is_symlink(): raise ValueError("prepared libslirp cache must not be a symlink")
    present = {p.relative_to(target).as_posix(): p.read_bytes() for p in sorted(target.rglob("*")) if p.is_file() or p.is_symlink()}
    if present != files or any(p.is_symlink() for p in target.rglob("*")):
        raise ValueError("prepared libslirp source changed; preserve and inspect the cache before rebuilding")
    return target

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive",type=Path,nargs="?");parser.add_argument("output",type=Path,nargs="?")
    parser.add_argument("--cached",action="store_true",help="Print the validated immutable cache for the app build")
    args=parser.parse_args()
    if args.cached:
        if args.archive or args.output: parser.error("--cached takes no paths")
        print(prepared_source(Path(__file__).resolve().parents[2]))
    elif not args.archive or not args.output: parser.error("archive and output are required")
    else: print(prepare(args.archive,args.output))
