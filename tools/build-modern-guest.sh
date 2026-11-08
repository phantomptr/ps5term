#!/usr/bin/env bash
# PS5Term - GPL-3.0-or-later. Build a modern candidate guest on Linux only.
# No console communication, application deployment or automatic release staging.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Linux || $# -lt 1 || $# -gt 2 || "${1:-}" != /* || "${1:-}" == *' '* ]]; then
    echo 'Usage (Linux): tools/build-modern-guest.sh /absolute/disposable/build-directory [--configure-only]' >&2
    exit 2
fi
if [[ "$(id -u)" == 0 ]]; then echo "Use an unprivileged Linux build user" >&2; exit 2; fi
work="$(realpath -m -- "$1")"
if [[ $# == 2 && "$2" != --configure-only ]]; then echo "Unknown option: $2" >&2; exit 2; fi
cd "$root"
case "$work/" in "$root/"*) echo 'Use a Linux-native directory outside the mounted repository' >&2; exit 2;; esac
mkdir -p "$work"
exec 9>"$work/.build.lock"
flock -n 9 || { echo 'This guest build is already running' >&2; exit 1; }
# Refuse stale outputs when the source/configuration changes. A different input
# fingerprint requires a new scratch directory, never an automatic deletion.
python3 - "$root" "$work" <<'PY'
import hashlib,json,platform,sys
from pathlib import Path
root,work=map(Path,sys.argv[1:])
files=[root/'tools/build-modern-guest.sh',root/'tools/dev/modern_guest.py',root/'tools/build-data-seed.sh',*sorted((root/'tooling/subsystem/guest').rglob('*'))]
inputs={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}
manifest=dict(schema=1,inputs=inputs,buildroot='2025.02.19',host_arch=platform.machine(),
              source_date_epoch=1735689600,jobs=2,ps5_runtime_verified=False)
stamp=work/'inputs.json'
if stamp.exists() and json.loads(stamp.read_text()) != manifest:
    raise SystemExit('Guest inputs changed; preserve this build and choose a fresh scratch directory')
if not stamp.exists(): stamp.write_text(json.dumps(manifest,indent=2)+'\n')
PY
if [[ ! -d "$work/source" ]]; then
    python3 -m tools.dev.modern_guest \
        --archive "$root/.deps/subsystem/downloads/buildroot-2025.02.19.tar.xz" \
        --destination "$work/source"
fi
source="$work/source/buildroot-2025.02.19"
export SOURCE_DATE_EPOCH=1735689600
export BR2_EXTERNAL="$root/tooling/subsystem/guest"
make -C "$source" O="$work/output" BR2_DEFCONFIG="$root/tooling/subsystem/guest/ps5term_defconfig" defconfig
if [[ "${2:-}" == --configure-only ]]; then exit 0; fi
make -C "$source" O="$work/output" BR2_JLEVEL=2 -j2
if [[ ! -e "$work/output/images/data-seed.ext2" ]]; then
    bash "$root/tools/build-data-seed.sh" "$work/output/host/sbin/mke2fs" "$work/output/images/data-seed.ext2"
fi
# Collect upstream manifests, licenses and the exact source archives required
# for this configuration. Treat legal-info warnings as release review items.
make -C "$source" O="$work/output" BR2_JLEVEL=2 legal-info
printf 'Candidate images: %s\nLicense/source bundle: %s\n' "$work/output/images" "$work/output/legal-info"
