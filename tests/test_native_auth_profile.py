# PS5Term - GPL-3.0-or-later.
import unittest
from tools.dev.native_auth_profile import select_profile, BASELINE_WORDS

class NativeAuthProfileTests(unittest.TestCase):
    def test_normal_build_has_no_auth_override(self):
        self.assertEqual(select_profile('default', []), {})

    def test_research_changes_exactly_the_rfork_capability_bit(self):
        result=select_profile('rfork-research', ['PS5TERM_CHILD_RESEARCH'])
        words=result['words']
        self.assertEqual(len(words),17)
        self.assertEqual(words[0],BASELINE_WORDS[0])
        self.assertEqual(words[1]^BASELINE_WORDS[1],1<<62)
        self.assertEqual(words[2:],list(BASELINE_WORDS[2:]))
        self.assertEqual(len(bytes.fromhex(result['auth_info'])),0x88)
        self.assertEqual(result['definition'],'PS5TERM_RFORK_AUTH_RESEARCH')

    def test_no_arbitrary_or_unlabelled_profile(self):
        for profile,defs in [('all-caps',[]),('rfork-research',[]),
                             ('default',['PS5TERM_RFORK_AUTH_RESEARCH']),
                             ('rfork-research',['PS5TERM_CHILD_RESEARCH=0']),
                             ('rfork-research',['PS5TERM_CHILD_RESEARCH','PS5TERM_CHILD_RESEARCH=0']),
                             ('rfork-research',['PS5TERM_CHILD_RESEARCH','PS5TERM_RFORK_AUTH_RESEARCH=0'])]:
            with self.assertRaises(ValueError):select_profile(profile,defs)
