# SPDX-License-Identifier: GPL-3.0-or-later
"""Host libraries must never cross the macOS/Linux shared-checkout boundary."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NativeDependencyCacheTests(unittest.TestCase):
    def test_bootstrap_selects_compiler_target_without_reusing_legacy_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'tools').mkdir()
            script = root/'tools/setup-native-dependencies.sh'
            shutil.copyfile(ROOT/'tools/setup-native-dependencies.sh', script)
            commands = root/'bin'; commands.mkdir()
            for command in ('wget', 'unzip', 'sha256sum', 'tar', 'make', 'llvm-ar-18', 'llvm-ranlib-18'):
                p = commands/command
                p.write_text('#!/bin/sh\nexit 91\n'); p.chmod(0o755)
            compiler = commands/'clang-18'
            compiler.write_text('#!/bin/sh\n[ "$1" = -dumpmachine ] || exit 92\nprintf "%s\\n" "$TEST_TARGET"\n')
            compiler.chmod(0o755)
            for target in ('legacy', 'aarch64-unknown-linux-gnu', 'arm64-apple-darwin24.0.0'):
                base = root/'.deps/native/zlib'
                if target != 'legacy': base /= target
                (base/'root/usr/lib').mkdir(parents=True)
                (base/'root/usr/include').mkdir(parents=True)
                (base/'root/usr/lib/libz.a').write_text(target)
                (base/'root/usr/include/zlib.h').touch()
                (base/'root/.source-version').write_text('1.3.2\n')
            for target in ('aarch64-unknown-linux-gnu', 'arm64-apple-darwin24.0.0'):
                result = subprocess.run(['/bin/bash', str(script), '--skip-sdk'], check=True,
                    capture_output=True, text=True,
                    env={**os.environ, 'PATH':str(commands)+':/usr/bin:/bin', 'TEST_TARGET':target})
                values = dict(line.split('=',1) for line in result.stdout.splitlines())
                library = Path(values['ZLIB_ARCHIVE'])
                self.assertEqual(library.read_text(), target)
                self.assertIn('/'+target+'/', str(library))

if __name__ == '__main__': unittest.main()
