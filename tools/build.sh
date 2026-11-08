#!/usr/bin/env bash
# ps5-native-app-boilerplate - Native Linux/WSL application build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles, links, signs, validates, and assembles the root skeleton app.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
format=${1:-Folder}
format=${format,,}
case "$format" in folder|ffpkg|ffpfsc|all) ;; *)
    echo "usage: tools/build.sh [Folder|Ffpkg|Ffpfsc|All]" >&2
    exit 2
esac

for command in python3 sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done
dependencies=$(bash "$root/tools/setup-native-dependencies.sh")
while IFS='=' read -r dependency value; do
    case "$dependency" in
        SDK_ROOT) sdk_root="$value" ;;
        ZLIB_INCLUDE) zlib_include="$value" ;;
        ZLIB_ARCHIVE) zlib_archive="$value" ;;
    esac
done <<< "$dependencies"

param="$root/sce_sys/param.json"
title_id=$(python3 - "$param" <<'PY'
import json, re, sys

with open(sys.argv[1], encoding="utf-8") as source:
    value = json.load(source)

title_id = value.get("titleId", "")
concept_id = value.get("conceptId", "")
content_id = value.get("contentId", "")
if not re.fullmatch(r"(?:PPSA|FAKE)\d{5}", title_id):
    raise SystemExit("param.json titleId must use PPSA or FAKE followed by five digits")
if not re.fullmatch(r"\d{5}", concept_id):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-(?:PPSA|FAKE)\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", value.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", value.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
size = value.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")

category = value.get("applicationCategoryType")
badge = value.get("contentBadgeType")
if (category, badge) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = value.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in value:
    raise SystemExit("media param.json must not contain gameIntent")

localized = value.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
print(title_id)
PY
)

# Loader/container constants validated on firmware 6.02 and 12.70. These are
# deliberately separate from the public application version in param.json.
module_sdk=0x02000009
companion_sdk=0x08050001
fself_magic=0x1D3D154F

bash "$root/tools/validate-assets.sh" "$root/sce_sys"

cxx=${CXX:-}
if [[ -z $cxx ]]; then
    cxx=$(command -v clang++-18 || command -v clang++)
fi
[[ -n $cxx ]] || { echo "Clang++ was not found" >&2; exit 2; }

build="$root/build"
dist="$root/dist"
native="$root/tooling/native"
tool="$build/host/ps5-native-tool"
mkdir -p "$build/host" "$build/obj" "$dist"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "$zlib_include" \
    "$native/native_app_builder.cpp" "$native/self_container.cpp" \
    "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
    "$zlib_archive" -o "$tool"

mapfile -d '' -t source_paths < <(
    find "$root/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
        -print0 | sort -z
)
sources=()
for source in "${source_paths[@]}"; do
    sources+=("${source#"$root/"}")
done
python3 "$root/tools/verify-vterm.py"
for source in "$root"/third_party/libvterm/src/*.c; do
    sources+=("${source#"$root/"}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C or C++ sources" >&2; exit 2; }

definitions=()
includes=()
archives=()
pacbrew_packages=()
pacbrew_includes=()
pacbrew_archives=()
# The deployable app owns SSH/SFTP. An explicitly empty override is reserved
# for legacy comparison builds; an unset variable selects the standalone app.
app_definitions=${APP_DEFINITIONS-'PS5TERM_OWNED_SSH PS5TERM_OWNED_SFTP PS5TERM_LINUX_SUBSYSTEM PS5TERM_LINUX_PERSISTENCE PS5TERM_LINUX_SHARED_FOLDER PS5TERM_LINUX_NETWORK'}
[[ -z $app_definitions ]] || read -r -a definitions <<< "$app_definitions"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a includes <<< "$APP_INCLUDE_PATHS"
[[ -z ${APP_STATIC_ARCHIVES:-} ]] || read -r -a archives <<< "$APP_STATIC_ARCHIVES"
[[ -z ${PACBREW_PACKAGES:-} ]] || read -r -a pacbrew_packages <<< "$PACBREW_PACKAGES"
[[ -z ${PACBREW_INCLUDE_PATHS:-} ]] || read -r -a pacbrew_includes <<< "$PACBREW_INCLUDE_PATHS"
[[ -z ${PACBREW_STATIC_ARCHIVES:-} ]] || read -r -a pacbrew_archives <<< "$PACBREW_STATIC_ARCHIVES"

# Build the app-owned service directly; no external SSH payload is needed.
for definition in "${definitions[@]}"; do
    if [[ $definition == PS5TERM_OWNED_SSH ]]; then
        APP_DEFINITIONS="${definitions[*]}" python3 "$root/tools/build-owned-ssh-native.py"
        definitions+=(WOLFSSL_USER_SETTINGS)
        includes+=(. backend/owned_ssh build/owned-ssh-native/wolfssl-5.9.4-stable
                   build/owned-ssh-native/wolfssh-1.6.0-stable)
        archives+=(build/owned-ssh-native/libowned-ssh.a)
    fi
done

# Custom executable auth metadata is confined to a labelled child-research build.
# The default leaves the loader's ordinary policy untouched.
auth_arguments=()
auth_selection=(--profile "${APP_AUTH_PROFILE:-default}")
for definition in "${definitions[@]}"; do auth_selection+=(--definition "$definition"); done
auth_profile=$(python3 "$root/tools/dev/native_auth_profile.py" "${auth_selection[@]}")
if [[ $auth_profile != '{}' ]]; then
    mapfile -t auth_fields < <(python3 -c \
        'import json,sys; p=json.loads(sys.argv[1]); print(p["definition"]); print(p["authority"]); print(p["auth_info"])' \
        "$auth_profile")
    [[ ${#auth_fields[@]} == 3 ]] || { echo "invalid executable auth profile" >&2; exit 2; }
    definitions+=("${auth_fields[0]}")
    auth_arguments=(--authority "${auth_fields[1]}" --auth-info "${auth_fields[2]}")
fi

pacbrew_cflags=()
pacbrew_libs=()
if (( ${#pacbrew_packages[@]} > 0 || ${#pacbrew_includes[@]} > 0 || ${#pacbrew_archives[@]} > 0 )); then
    pacbrew_resolution=$(bash "$root/tools/setup-pacbrew-dependencies.sh" \
        --resolve "${pacbrew_packages[@]}")
    mapfile -d '' -t pacbrew_cflags < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' \
        "$pacbrew_resolution")
    mapfile -d '' -t pacbrew_libs < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"]]' \
        "$pacbrew_resolution")
    pacbrew_root=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["root"])' \
        "$pacbrew_resolution")
    for include in "${pacbrew_includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$ &&
            -d $pacbrew_root/user/homebrew/$include ]] || {
            echo "invalid PacBrew include path: $include" >&2; exit 2;
        }
        pacbrew_cflags+=("-I$pacbrew_root/user/homebrew/$include")
    done
    for archive in "${pacbrew_archives[@]}"; do
        [[ $archive =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*\.a$ &&
            -f $pacbrew_root/user/homebrew/$archive ]] || {
            echo "invalid PacBrew static archive: $archive" >&2; exit 2;
        }
        pacbrew_libs+=("$pacbrew_root/user/homebrew/$archive")
    done
    printf 'PacBrew dependencies: %s\n' "${pacbrew_packages[*]:-(manual archives)}"
fi

# Subsystem compilation is explicit and fixed before sources and identity.
linux_subsystem=false
tinyemu_source=""
for definition in "${definitions[@]}"; do
    if [[ $definition == PS5TERM_LINUX_SUBSYSTEM ]]; then
        linux_subsystem=true
        # Fetches the pinned, hash-checked interpreter archive on a fresh checkout.
        (cd "$root" && python3 -c 'from tools.dev.embedded_linux import prepare; prepare(True)') >/dev/null
        tinyemu_source=$(cd "$root" && python3 -m tools.dev.prepare_embedded_source)
    fi
done

identity_args=()
for definition in "${definitions[@]}"; do identity_args+=(--definition "$definition"); done
python3 "$root/tools/generate-build-info.py" --root "$root" --output "$build/generated" "${identity_args[@]}"

objects=()
for source in "${sources[@]}"; do
    [[ $source =~ ^(src/[A-Za-z0-9_./-]+\.(c|cc|cpp)|third_party/libvterm/src/[A-Za-z0-9_-]+\.c)$ && -f $root/$source ]] || {
        echo "invalid source: $source" >&2; exit 2;
    }
    object="$build/obj/${source//\//_}.o"
    if [[ $source == *.c ]]; then standard=-std=c11; else standard=-std=c++20; fi
    args=("$standard" -O2 -Wall -Wextra -ffunction-sections -fdata-sections
        -I"$build/generated" -I"$root/third_party/libvterm/include" -I"$root/backend/subsystem" -DPS5TERM_GENERATED_BUILD_INFO)
    [[ $source == *.c ]] || args+=(-fno-exceptions -fno-rtti)
    for definition in "${definitions[@]}"; do
        [[ $definition =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$ ]] || {
            echo "invalid compile definition: $definition" >&2; exit 2;
        }
        args+=("-D$definition")
    done
    for include in "${includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -d $root/$include ]] || {
            echo "invalid include path: $include" >&2; exit 2;
        }
        args+=("-I$root/$include")
    done
    args+=("${pacbrew_cflags[@]}")
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
        "${args[@]}" -c "$root/$source" -o "$object"
    objects+=("$object")
done

crt_definitions=()
for definition in "${definitions[@]}"; do crt_definitions+=("-D$definition"); done
PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections "${crt_definitions[@]}" \
    -c "$native/app_crt.cpp" -o "$build/obj/app_crt.o"

PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$build/obj/app_cpp_runtime.o"

# --- TinyEMU Linux subsystem (Tier C) ---
subsystem="$root/backend/subsystem"
if $linux_subsystem; then
    tinyemu_engine_sources=(virtio.c pci.c fs.c cutils.c iomem.c simplefb.c softfp.c riscv_machine.c)
    tinyemu_cpu_widths=(32 64)
    tinyemu_flags=(-std=gnu11 -O2 -fwrapv -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
        -DCONFIG_RISCV_MAX_XLEN=64 -DPS5TERM_TINYEMU_BUILTIN_BSWAP -DPS5TERM_EMBEDDED
        -I"$tinyemu_source" -I"$subsystem"
        -include "$subsystem/tinyemu_hooks.h"
        -ffunction-sections -fdata-sections
        -fno-sanitize=shift-base)
    tinyemu_objects=()
    for src in "${tinyemu_engine_sources[@]}"; do
        name="${src%.c}"
        obj="$build/obj/tinyemu_${name}.o"
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            "${tinyemu_flags[@]}" -c "$tinyemu_source/$src" -o "$obj"
        tinyemu_objects+=("$obj")
    done
    for width in "${tinyemu_cpu_widths[@]}"; do
        obj="$build/obj/tinyemu_riscv_cpu_${width}.o"
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            "${tinyemu_flags[@]}" -DMAX_XLEN="$width" \
            -c "$tinyemu_source/riscv_cpu.c" -o "$obj"
        tinyemu_objects+=("$obj")
    done
    # Adapter objects (C).
    for adapter_src in embedded_linux.c linux_session.c; do
        name="${adapter_src%.c}"
        obj="$build/obj/subsystem_${name}.o"
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            -std=gnu11 -O2 -Wall -Wextra -fwrapv -pthread \
            -DPS5TERM_TINYEMU_BUILTIN_BSWAP -DPS5TERM_EMBEDDED \
            -I"$tinyemu_source" -I"$subsystem" \
            -ffunction-sections -fdata-sections \
            -c "$subsystem/$adapter_src" -o "$obj"
        tinyemu_objects+=("$obj")
    done
    printf 'TinyEMU: compiled %d objects\n' "${#tinyemu_objects[@]}"
else
    tinyemu_objects=()
    printf 'TinyEMU: disabled (opt in with PS5TERM_LINUX_SUBSYSTEM)\n'
fi

# --- Optional in-app userspace NAT: pinned libslirp profile (BSD-3-Clause) ---
# Outbound guest IPv4 only. No host forwarding, process spawn, TFTP or IPv6.
linux_network=false
owned_ssh=false
for definition in "${definitions[@]}"; do
    if [[ $definition == PS5TERM_LINUX_NETWORK ]]; then linux_network=true; fi
    if [[ $definition == PS5TERM_OWNED_SSH ]]; then owned_ssh=true; fi
done
if $linux_network; then
    $linux_subsystem || { echo "PS5TERM_LINUX_NETWORK requires PS5TERM_LINUX_SUBSYSTEM" >&2; exit 2; }
    slirp_source=$(cd "$root" && python3 -m tools.dev.prepare_slirp --cached)
    [[ -d $slirp_source/src && -f $slirp_source/LICENSE ]] || { echo "prepared libslirp source missing" >&2; exit 2; }
    network_flags=(-std=gnu11 -O2 -DPS5TERM_NATIVE_NET -I"$subsystem/net" -I"$slirp_source/src"
        -ffunction-sections -fdata-sections)
    network_objects=()
    for src in "$slirp_source"/src/*.c; do
        name=$(basename "$src" .c)
        [[ $name =~ ^[A-Za-z0-9_-]+$ ]] || { echo "invalid libslirp source: $name" >&2; exit 2; }
        obj="$build/obj/slirp_${name}.o"
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            "${network_flags[@]}" -c "$src" -o "$obj"
        network_objects+=("$obj")
    done
    for src in "$subsystem"/net/*.c; do
        name=$(basename "$src" .c)
        obj="$build/obj/net_${name}.o"
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            "${network_flags[@]}" -Wall -Wextra -Werror -c "$src" -o "$obj"
        network_objects+=("$obj")
    done
    if ! $owned_ssh; then
        # The owned SSH archive otherwise supplies the kernel entropy adapter.
        PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
            -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
            -c "$root/backend/owned_ssh/native_entropy.c" -o "$build/obj/net_native_entropy.o"
        network_objects+=("$build/obj/net_native_entropy.o")
    fi
    tinyemu_objects+=("${network_objects[@]}")
    printf 'Network: compiled %d bounded libslirp profile objects\n' "${#network_objects[@]}"
else
    printf 'Network: disabled (opt in with PS5TERM_LINUX_NETWORK)\n'
fi

# The SDK's crt1.o provides kernel_* APIs, __crt_syscall, and __kernel_init.
# It also defines _start which conflicts with app_crt.o. Weaken _start and
# __crt_start so the project's own _start takes priority, then wrap in an
# archive so only referenced symbols are pulled in.
"$sdk_root/bin/llvm-objcopy" \
    --redefine-sym _start=__sdk_crt_entry \
    --redefine-sym __crt_start=__sdk_crt_start_impl \
    "$sdk_root/target/lib/crt1.o" "$build/crt1_kernel.o"
rm -f "$build/libps5_kernel_api.a"
"$sdk_root/bin/llvm-ar" rcs "$build/libps5_kernel_api.a" "$build/crt1_kernel.o"
link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}" \
    "${tinyemu_objects[@]}" \
    "$build/libps5_kernel_api.a" "$sdk_root/target/lib/libc.a")
if (( ${#tinyemu_objects[@]} > 0 )); then
    link_inputs+=("$sdk_root/target/lib/libpthread.a")
fi
for archive in "${archives[@]}"; do
    [[ $archive =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.a$ && -f $root/$archive ]] || {
        echo "invalid static archive: $archive" >&2; exit 2;
    }
    link_inputs+=("$root/$archive")
done
if (( ${#pacbrew_libs[@]} > 0 )); then
    link_inputs+=(--start-group "${pacbrew_libs[@]}" --end-group)
fi
# Import stubs for system modules the pinned SDK has no stub for. libSceMouse
# (src/mouse.cpp) is generated on every build; APP_EXTRA_STUBS can add more
# (.so paths relative to the repo, from tools/make-stub.sh). Linked
# --as-needed like the SDK stubs.
bash "$root/tools/make-stub.sh" "$build/stubs/libSceMouse.so" libSceMouse.sprx \
    sceMouseInit sceMouseOpen sceMouseRead sceMouseClose
extra_stubs=(build/stubs/libSceMouse.so)
stub_options=()
requested_stubs=()
[[ -z ${APP_EXTRA_STUBS:-} ]] || read -r -a requested_stubs <<< "$APP_EXTRA_STUBS"
for stub in "${requested_stubs[@]}"; do
    [[ $stub == build/stubs/libSceMouse.so ]] || extra_stubs+=("$stub")
done
for stub in "${extra_stubs[@]}"; do
    [[ $stub =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.so$ && -f $root/$stub ]] || {
        echo "invalid extra stub: $stub" >&2; exit 2;
    }
    stub_options+=(--stub "$root/$stub")
done
extra_stub_paths=("${extra_stubs[@]/#/$root/}")
"$sdk_root/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
    --gc-sections --version-script "$native/app-symbols.map" \
    -e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}" \
    --as-needed "$sdk_root"/target/lib/libkernel.so "$sdk_root"/target/lib/*.so "${extra_stub_paths[@]}"
# A raw syscall outside libkernel kills the app on the console (CE-108255-1), and
# no host test can see it. Research builds that intentionally use them are exempt.
objdump=$(command -v llvm-objdump-18 || command -v llvm-objdump || true)
research_build=false
for definition in "${definitions[@]}"; do
    case "$definition" in PS5TERM_RESEARCH|PS5TERM_CAPABILITY_RESEARCH|PS5TERM_NATIVE_PTY_RESEARCH|PS5TERM_LEGACY_SHELL_RESEARCH) research_build=true ;; esac
done
if [[ -n $objdump ]] && ! $research_build; then
    raw_syscalls=$("$objdump" -d --no-show-raw-insn "$build/llvm-pie.elf" | awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /[[:space:]]syscall$/ {print fn}' | sort -u | tr '\n' ' ')
    if [[ -n $raw_syscalls ]]; then
        echo "raw syscall instructions in the app would crash it on the console: $raw_syscalls" >&2
        exit 2
    fi
fi
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    "${stub_options[@]}" \
    --stub-dir "$sdk_root/target/lib" --module-sdk "$module_sdk" \
    --companion-sdk "$companion_sdk" --file-name eboot.elf

# Reject incongruent LOAD mappings before packaging. The converter supports
# constant pointer tables before the GOT; the segment must start at RELRO's
# actual origin rather than the GOT's later file offset.
python3 - "$build/eboot.elf" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
phoff, = struct.unpack_from("<Q", data, 0x20)
phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
for index in range(phnum):
    kind, _, offset, vaddr, _, _, _, align = struct.unpack_from(
        "<IIQQQQQQ", data, phoff + index * phentsize)
    if kind == 1 and align > 1 and offset % align != vaddr % align:
        raise SystemExit(
            f"eboot.elf LOAD segment {index}: offset {offset:#x} vs address {vaddr:#x} "
            f"(align {align:#x}); invalid converter segment mapping")
PY

app="$dist/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$build/eboot.elf" --out "$app/eboot.bin" \
    --magic "$fself_magic" "${auth_arguments[@]}"

cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $root/sce_sys/$asset ]] && cp "$root/sce_sys/$asset" "$app/sce_sys/$asset"
done
mkdir -p "$app/assets"
cp "$build/generated/build-info.json" "$app/assets/build-info.json"
python3 "$root/tools/app_distribution.py" --root "$root" --app "$app"

[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"
(cd "$root/runtime" && sha256sum --check --strict libc.prx.sha256)
runtime_modules=("$root/runtime/libc.prx")
additional_runtime=()
[[ -z ${APP_RUNTIME_MODULES:-} ]] || read -r -a additional_runtime <<< "$APP_RUNTIME_MODULES"
for source in "${additional_runtime[@]}"; do
    [[ $source =~ ^\.local/runtime/[A-Za-z0-9._-]+\.prx$ && -f $root/$source ]] || {
        echo "invalid runtime module: $source" >&2; exit 2;
    }
    runtime_modules+=("$root/$source")
done
declare -A runtime_names=()
for input in "${runtime_modules[@]}"; do
    name=${input##*/}
    [[ $name =~ ^[A-Za-z0-9._-]+\.prx$ && -z ${runtime_names[$name]+present} ]] || {
        echo "invalid or duplicate runtime module name: $name" >&2; exit 2;
    }
    runtime_names[$name]=present
    magic=$(python3 - "$input" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    print(f"{struct.unpack('<I', stream.read(4))[0]:08x}")
PY
)
    if [[ $magic == 1d3d154f || $magic == eef51454 ]]; then
        cp "$input" "$app/sce_module/$name"
    else
        "$tool" self --sign --in "$input" --out "$app/sce_module/$name"
    fi
    "$tool" self --inspect --file "$app/sce_module/$name"
done
"$tool" self --inspect --file "$app/eboot.bin"

if [[ $format == ffpkg || $format == all ]]; then
    ufs2tool=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpkg)
    rm -f -- "$dist/$title_id.ffpkg"
    "$ufs2tool" makefs -S 4096 -b 20% -t ffs \
        -o version=2,bsize=32768,fsize=4096,minfree=0,softupdates=0,optimization=space \
        "$dist/$title_id.ffpkg" "$app"
    python3 - "$dist/$title_id.ffpkg" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    stream.seek(0x1055c)
    if struct.unpack("<I", stream.read(4))[0] != 0x19540119:
        raise SystemExit("FFPKG is missing the UFS2 superblock magic")
PY
fi
if [[ $format == ffpfsc || $format == all ]]; then
    mkpfs=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpfsc)
    rm -f -- "$dist/$title_id.ffpfsc"
    "$mkpfs" pack folder --no-adjust-output-file-extension \
        --version PS5 --verify "$app" "$dist/$title_id.ffpfsc"
fi

printf 'Build complete.\nApp folder: %s\n' "$app"
[[ $format != ffpkg && $format != all ]] || printf 'FFPKG:     %s\n' "$dist/$title_id.ffpkg"
[[ $format != ffpfsc && $format != all ]] || printf 'FFPFSC:    %s\n' "$dist/$title_id.ffpfsc"
