#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.

"""Run with: python3 .github/scripts/ybbld_targets_test.py"""

import unittest

from ybbld_targets import glob_to_regex, targets_for


class GlobTest(unittest.TestCase):
    def check(self, glob, matching, non_matching):
        regex = glob_to_regex(glob)
        for path in matching:
            self.assertTrue(regex.match(path), f'{glob} should match {path}')
        for path in non_matching:
            self.assertFalse(regex.match(path), f'{glob} should not match {path}')

    def test_single_star_stays_in_directory(self):
        self.check('.arc*', ['.arcconfig', '.arclint'], ['.arc/x', 'a/.arcconfig'])

    def test_double_star_crosses_directories(self):
        self.check('docs/**', ['docs/a.md', 'docs/content/a/b.md'], ['docs', 'xdocs/a.md'])

    def test_leading_double_star_includes_root(self):
        self.check('**/README*', ['README.md', 'src/yb/README', 'a/b/README.txt'],
                   ['src/READMEs/x', 'src/xREADME'])

    def test_plain_path_matches_exactly(self):
        self.check('java/yb-client', ['java/yb-client'], ['java/yb-client/pom.xml'])
        self.check('NOTICE.txt', ['NOTICE.txt'], ['NOTICEXtxt', 'a/NOTICE.txt'])

    def test_unsupported_syntax_is_rejected(self):
        for glob in ('a?b', 'a+', '[ab]'):
            with self.assertRaises(ValueError):
                glob_to_regex(glob)


class TargetsTest(unittest.TestCase):
    def check(self, paths, expected):
        self.assertEqual(targets_for(paths), expected, paths)

    def test_db_code(self):
        self.check(['src/yb/tablet/tablet.cc'], ['DB'])
        self.check(['src/postgres/src/backend/a.c'], ['DB'])
        self.check(['java/yb-client/pom.xml'], ['DB'])

    def test_non_build_files(self):
        self.check(['.github/workflows/bld.yml'], ['NO-BLD'])
        self.check(['.claude/skills/x/SKILL.md', 'architecture/design/x.md'], ['NO-BLD'])
        self.check(['src/lint/postgres.sh'], ['NO-BLD'])

    def test_readme(self):
        self.check(['src/yb/README'], ['NO-BLD'])
        self.check(['README.md'], ['NO-BLD', 'DOCS'])

    def test_docs(self):
        self.check(['docs/content/stable/a.md'], ['DOCS'])

    def test_yba(self):
        self.check(['managed/src/main/java/A.java'], ['YBA'])
        self.check(['managed/ui/src/a.tsx'], ['YBA-UI'])
        self.check(['managed/yba-installer/cmd/a.go'], ['YBA-INSTALL'])
        self.check(['managed/yba-cli/cmd/a.go'], ['YBA', 'YBA-CLI'])
        self.check(['managed/devops/bin/yb_platform_backup.sh'], ['YBA', 'YBA-INSTALL'])

    def test_ybc_version_also_builds_db(self):
        self.check(['managed/src/main/resources/ybc_version.conf'], ['DB', 'YBA'])

    def test_mixed_pr_selects_every_target(self):
        self.check(['src/yb/a.cc', 'docs/a.md', '.github/x', 'managed/ui/a.ts'],
                   ['DB', 'NO-BLD', 'DOCS', 'YBA-UI'])

    def test_db_skipped_when_every_file_is_ignored(self):
        self.check(['docs/a.md', 'managed/A.java'], ['DOCS', 'YBA'])

    def test_empty(self):
        self.check([], [])
        self.check([''], [])

    def test_every_path_selects_a_target(self):
        for path in ('a', 'src/yb/a.cc', '.gitignore', 'code_style', 'managed/x', 'docs/x',
                     '.arcconfig', 'cloud/x', 'LICENSE.md', 'license/x', 'sample/x'):
            self.assertTrue(targets_for([path]), path)


if __name__ == '__main__':
    unittest.main()
