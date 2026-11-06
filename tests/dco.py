# SPDX-License-Identifier: MPL-2.0
"""Exercise DCO policy against isolated Git history, never the user's commits."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('check_dco', Path(__file__).resolve().parents[1] / 'scripts/check_dco.py')
dco = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dco)


class DCOTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='afterlight-dco-')
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.env = os.environ.copy()
        self.env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1',
                        GIT_AUTHOR_NAME='DCO Fixture', GIT_AUTHOR_EMAIL='fixture@example.com',
                        GIT_COMMITTER_NAME='Fixture Committer', GIT_COMMITTER_EMAIL='committer@example.com')
        self.git('init', '-q')
        self.base = self.commit('Existing unsigned history')

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.repo), *args], env=self.env,
                              check=True, capture_output=True, text=True).stdout.strip()

    def commit(self, message, *options):
        self.git('-c', 'commit.gpgsign=false', 'commit', '-q', '--allow-empty', '-m', message, *options)
        return self.git('rev-parse', 'HEAD')

    def check(self):
        return dco.check(self.repo, self.base, 'HEAD')

    def test_unsigned_history_is_excluded(self):
        self.commit('Change\n\nSigned-off-by: DCO Fixture <fixture@example.com>')
        self.assertEqual(self.check(), (1, []))

    def test_missing_signoff(self):
        bad = self.commit('Unsigned change')
        self.assertEqual(self.check()[1], [(bad, 'DCO Fixture', 'fixture@example.com')])

    def test_wrong_author_or_committer_only(self):
        for signer in ['Other Name <fixture@example.com>', 'DCO Fixture <wrong@example.com>', 'Fixture Committer <committer@example.com>']:
            self.commit('Change\n\nSigned-off-by: ' + signer)
        self.assertEqual(len(self.check()[1]), 3)

    def test_body_text_is_not_a_trailer(self):
        self.commit('Discussion\n\nSigned-off-by: DCO Fixture <fixture@example.com>\n\nThis is body prose after the example, not a sign-off.')
        self.assertEqual(len(self.check()[1]), 1)

    def test_all_introduced_commits_are_checked(self):
        bad = self.commit('Unsigned first change')
        self.commit('Signed follow-up\n\nSigned-off-by: DCO Fixture <fixture@example.com>')
        count, failures = self.check()
        self.assertEqual(count, 2)
        self.assertEqual(failures[0][0], bad)

    def test_multiple_trailers(self):
        self.commit('Change\n\nReviewed-by: Reviewer <reviewer@example.com>\nSigned-off-by: Other Contributor <other@example.com>\nSigned-off-by: DCO Fixture <fixture@example.com>')
        self.assertEqual(self.check(), (1, []))

    def test_invalid_revision_fails_closed(self):
        with self.assertRaises(subprocess.CalledProcessError):
            dco.check(self.repo, '--all', 'HEAD')


if __name__ == '__main__':
    unittest.main()
