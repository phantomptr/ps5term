# PS5Term reproducible source identity. GPL-3.0-or-later.
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class BuildInfoTests(unittest.TestCase):
    def test_identity_changes_for_product_source_not_private_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'src').mkdir(); (root/'.local').mkdir()
            (root/'src/main.cpp').write_text('int main() {}')
            def identity():
                result = subprocess.run(['python3', str(ROOT/'tools/generate-build-info.py'),
                                         '--root', str(root), '--output', str(root/'generated')],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return json.loads((root/'generated/build-info.json').read_text())
            first = identity()
            (root/'.local/secret').write_text('PRIVATE_NOT_IN_MANIFEST')
            self.assertEqual(first, identity())
            (root/'src/main.cpp').write_text('int main() { return 1; }')
            self.assertNotEqual(first['build_id'], identity()['build_id'])
            self.assertNotIn('PRIVATE', (root/'generated/build-info.json').read_text())
            self.assertIn('PS5TERM_BUILD_ID', (root/'generated/build_identity.hpp').read_text())

    def test_compile_definitions_are_part_of_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def identity(definitions):
                command = ['python3', str(ROOT/'tools/generate-build-info.py'),
                           '--root', str(root), '--output', str(root/'generated')]
                for definition in definitions:
                    command += ['--definition', definition]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return json.loads((root/'generated/build-info.json').read_text())
            ordinary = identity([])
            research = identity(['PS5TERM_CAPABILITY_RESEARCH'])
            self.assertNotEqual(ordinary['build_id'], research['build_id'])
            self.assertEqual(identity(['B=1', 'A']), identity(['A', 'B=1', 'A']))
            self.assertEqual(research['definitions'], ['PS5TERM_CAPABILITY_RESEARCH'])
