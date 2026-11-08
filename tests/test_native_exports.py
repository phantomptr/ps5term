# GPL-3.0-or-later.
import struct
import unittest
from tools.dev.audit_native_exports import audit, nid


def image(symbol='fork', defined=True):
    data = bytearray(1024)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 0, ident, 3, 62, 1, 0, 64, 0, 0, 64, 56, 2, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0x1000, 0, len(data), len(data), 4096)
    struct.pack_into('<IIQQQQQQ', data, 120, 2, 4, 192, 0x10c0, 0, 80, 80, 8)
    for i, pair in enumerate([(5, 0x1140), (10, 64), (6, 0x11a0), (11, 24), (0x6100003f, 48)]):
        struct.pack_into('<qQ', data, 192 + 16*i, *pair)
    name = (nid(symbol) + '#I#A').encode() + b'\0'
    data[321:321+len(name)] = name
    struct.pack_into('<IBBHQQ', data, 440, 1, 0x12, 0, 1 if defined else 0, 0x1200, 24)
    return data


class NativeExportTests(unittest.TestCase):
    def test_nid_matches_existing_writer_known_export(self):
        self.assertEqual(nid('fork'), 'cjt-4ySmYTs')
        self.assertEqual(nid('rfork_thread'), 'bSDxEpGzmUE')

    def test_sectionless_export_and_missing_are_distinct(self):
        report = audit(image(), ['fork', 'vfork'])
        self.assertEqual(report['exports']['fork'][0]['value'], 0x1200)
        self.assertEqual(report['exports']['vfork'], [])
        self.assertFalse(report['runtime_support_proven'])

    def test_import_is_not_an_export(self):
        self.assertEqual(audit(image(defined=False), ['fork'])['exports']['fork'], [])

    def test_rejects_truncation_and_unmapped_tables(self):
        for bad in [image()[:100], b'not ELF']:
            with self.assertRaises(ValueError): audit(bad, ['fork'])
        bad = image()
        struct.pack_into('<Q', bad, 200, 0xffffffffffffffff)
        with self.assertRaises(ValueError): audit(bad, ['fork'])

    def test_rejects_bad_symbol_size_and_unterminated_name(self):
        bad = image()
        struct.pack_into('<Q', bad, 264, 47)
        with self.assertRaises(ValueError): audit(bad, ['fork'])
        bad = image(); bad[321:384] = b'x'*63
        with self.assertRaises(ValueError): audit(bad, ['fork'])
