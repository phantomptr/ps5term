# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject wrong-architecture or oversized images before candidate staging."""
import struct
import tempfile
from pathlib import Path
import unittest
from tools.dev.guest_images import inspect_images


class GuestImagesTests(unittest.TestCase):
    def fixture(self, directory):
        (directory / 'fw_jump.bin').write_bytes(b'firmware')
        kernel = bytearray(4096)
        struct.pack_into('<QQQ', kernel, 8, 0x200000, 4096, 0)
        kernel[56:60] = b'RSC\x05'
        (directory / 'Image').write_bytes(kernel)
        disk = bytearray(4096)
        disk[1080:1082] = b'\x53\xef'
        (directory / 'rootfs.ext2').write_bytes(disk)

    def test_describes_bounded_image_hashes_and_load_addresses(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            result = inspect_images(root)
            self.assertEqual(result['kernel-riscv64.bin']['load_address'], '0x80200000')
            self.assertEqual(result['root-riscv64.bin']['bytes'], 4096)
            self.assertEqual(len(result['bbl64.bin']['sha256']), 64)

    def test_optional_seed_is_checked_and_described(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root)
            seed=bytearray(16*1024*1024);seed[1080:1082]=b'\x53\xef'
            (root/'data-seed.ext2').write_bytes(seed)
            self.assertEqual(inspect_images(root)['data-seed.ext2']['bytes'],len(seed))
            (root/'data-seed.ext2').write_bytes(b'invalid')
            with self.assertRaises(ValueError):inspect_images(root)

    def test_rejects_wrong_kernel_offset_magic_endianness_and_bad_disk(self):
        corruptions = [('Image', 8, b'\0' * 8), ('Image', 56, b'ARM!'),
                       ('Image', 24, b'\x01'), ('rootfs.ext2', 1080, b'NO')]
        for filename, offset, data in corruptions:
            with self.subTest(file=filename, offset=offset), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.fixture(root)
                path = root / filename
                image = bytearray(path.read_bytes())
                image[offset:offset + len(data)] = data
                path.write_bytes(image)
                with self.assertRaises(ValueError):
                    inspect_images(root)

    def test_rejects_symlink_and_disk_over_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            (root / 'rootfs.ext2').unlink()
            (root / 'rootfs.ext2').symlink_to(root / 'Image')
            with self.assertRaises(ValueError):
                inspect_images(root)
            (root / 'rootfs.ext2').unlink()
            with (root / 'rootfs.ext2').open('wb') as out:
                out.truncate(32 * 1024 * 1024 + 512)
            with self.assertRaises(ValueError):
                inspect_images(root)


if __name__ == '__main__':
    unittest.main()
