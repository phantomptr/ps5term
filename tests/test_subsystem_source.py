# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned source preparation, independent of host build caches."""
from pathlib import Path
import tempfile
import unittest
from tools.dev.prepare_embedded_source import validate_tree, source_identity

class SubsystemSourceTests(unittest.TestCase):
    def test_profile_changes_invalidate_prepared_source(self):
        self.assertEqual(source_identity('archive',{'a':'one','b':'two'}),
                         source_identity('archive',{'b':'two','a':'one'}))
        self.assertNotEqual(source_identity('archive',{'a':'one'}),
                            source_identity('archive',{'a':'two'}))

    def test_validation_rejects_changed_added_or_linked_sources(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'file.c';p.write_text('pinned')
            manifest={'file.c':hashlib.sha256(p.read_bytes()).hexdigest()}
            validate_tree(root,manifest)
            p.write_text('changed')
            with self.assertRaises(ValueError):validate_tree(root,manifest)
            p.write_text('pinned');(root/'extra.c').write_text('extra')
            with self.assertRaises(ValueError):validate_tree(root,manifest)
            (root/'extra.c').unlink();p.unlink();p.symlink_to('/dev/null')
            with self.assertRaises(ValueError):validate_tree(root,manifest)

if __name__=='__main__':unittest.main()
