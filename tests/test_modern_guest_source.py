# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive extraction must preserve skeleton links without writing through links."""
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from tools.dev.modern_guest import extract_source


class SourceExtractionTests(unittest.TestCase):
    def archive(self, root, members):
        archive = root / 'source.tar'
        with tarfile.open(archive, 'w') as tar:
            for name, kind, value in members:
                item = tarfile.TarInfo(name)
                if kind == 'dir':
                    item.type = tarfile.DIRTYPE
                    tar.addfile(item)
                elif kind == 'link':
                    item.type = tarfile.SYMTYPE
                    item.linkname = value
                    tar.addfile(item)
                else:
                    data = value.encode()
                    item.size = len(data)
                    item.mode = 0o755
                    tar.addfile(item, io.BytesIO(data))
        return archive

    def test_preserves_reviewed_skeleton_link_and_executable_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = self.archive(root, [
                ('buildroot-2025.02.19', 'dir', ''),
                ('buildroot-2025.02.19', 'dir', ''),
                ('buildroot-2025.02.19/Makefile', 'file', 'all:\n'),
                ('buildroot-2025.02.19/package/skeleton-init-openrc/skeleton/etc/runlevels/default/sysv-rcs', 'link', '/etc/init.d/sysv-rcs'),
                ('buildroot-2025.02.19/support/run', 'file', '#!/bin/sh\n'),
                ('buildroot-2025.02.19/link', 'link', 'support/run'),
            ])
            destination = root / 'source'
            extract_source(archive, destination)
            self.assertTrue((destination / 'buildroot-2025.02.19/link').is_symlink())
            self.assertEqual((destination / 'buildroot-2025.02.19/support/run').stat().st_mode & 0o777, 0o755)

    def test_rejects_traversal_external_links_and_writes_beneath_links(self):
        cases = [
            [('../escaped', 'file', 'x')],
            [('buildroot-2025.02.19/link', 'link', '/tmp/escaped')],
            [('buildroot-2025.02.19/link', 'link', '../../escaped')],
            [('buildroot-2025.02.19/link/child', 'file', 'x'), ('buildroot-2025.02.19/link', 'link', 'elsewhere')],
            [('buildroot-2025.02.19/duplicate', 'file', 'x'), ('buildroot-2025.02.19/duplicate', 'file', 'y')],
        ]
        for members in cases:
            with self.subTest(members=members), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                archive = self.archive(root, members)
                with self.assertRaises(ValueError):
                    extract_source(archive, root / 'source')
                self.assertFalse((root / 'source').exists())


if __name__ == '__main__':
    unittest.main()
