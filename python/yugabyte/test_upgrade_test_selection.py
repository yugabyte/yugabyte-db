# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

"""Unit tests for upgrade_test_selection.py."""

import os
import pathlib

import pytest

from yugabyte import upgrade_test_selection as uts


MAPPING = '\n'.join([
    '# comment',
    'mode\tenforce',
    '-\t**/*-test.cc',
    'basic_upgrade-test\tsrc/yb/**',
    'auto_flag_upgrade-test\tsrc/yb/util/flags/*',
    'ycql_only_upgrade-test\tsrc/yb/yql/cql/**',
    'xcluster_upgrade-itest\tsrc/yb/xcluster/**',
    '',
])


def write_mapping(root: pathlib.Path, text: str) -> str:
    path = root / uts.MAPPING_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return str(root)


def test_mapping_file_parses() -> None:
    repo_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    with open(os.path.join(repo_root, uts.MAPPING_PATH)) as mapping_file:
        mapping = uts.parse_mapping(mapping_file.read())
    assert mapping.mode == 'log'
    assert len({binary for binary, _ in mapping.rules}) == 24
    assert len(mapping.rules) == 898
    assert len(mapping.ignore) == 3


def test_parse_mapping() -> None:
    mapping = uts.parse_mapping(MAPPING)
    assert mapping.mode == 'enforce'
    assert mapping.ignore == ['**/*-test.cc']
    assert mapping.rules[0] == ('basic_upgrade-test', 'src/yb/**')
    for bad in [
        'basic_upgrade-test\tsrc/yb/**',                       # no mode line
        'mode\tlog\nmode\tlog',                                # two mode lines
        'mode\tsometimes',                                     # unknown mode
        'mode\tlog\nbasic_upgrade-test',                       # one column
        'mode\tlog\nbasic_upgrade-test\tsrc/yb\textra',        # three columns
        'mode\tlog\nsrc/basic_upgrade-test\tsrc/yb',           # a path, not a binary
        'mode\tlog\nbasic_upgrade-test\t/src/yb',              # absolute
        'mode\tlog\nbasic_upgrade-test\tsrc/yb/',              # trailing slash
        'mode\tlog\nbasic_upgrade-test\tsrc/../yb',            # dot-dot
        'mode\tlog\nbasic_upgrade-test\tsrc/yb**',             # ** inside an element
        'mode\tlog\nbasic_upgrade-test\tsrc/yb\nbasic_upgrade-test\tsrc/yb',  # repeated
    ]:
        with pytest.raises(ValueError):
            uts.parse_mapping(bad)


@pytest.mark.parametrize('glob,path,expected', [
    ('src/yb/**', 'src/yb/master/catalog_manager.cc', True),
    ('src/yb/**', 'src/yb', True),
    ('src/yb/**', 'src/ybx/a.cc', False),
    ('src/yb/master/*', 'src/yb/master/a.cc', True),
    ('src/yb/master/*', 'src/yb/master/sub/a.cc', False),
    ('src/yb/master/ts_manager.*', 'src/yb/master/ts_manager.h', True),
    ('src/yb/master/ts_manager.*', 'src/yb/master/ts_manager_x.h', False),
    ('**/*-test.cc', 'src/yb/master/master-test.cc', True),
    ('CMakeLists.txt', 'src/CMakeLists.txt', False),
    ('build-support/thirdparty_archives*.yml', 'build-support/thirdparty_archives_x.yml', True),
])
def test_match_glob(glob: str, path: str, expected: bool) -> None:
    assert uts.match_glob(glob, path) == expected


def test_select_binaries() -> None:
    mapping = uts.parse_mapping(MAPPING)
    selected = uts.select_binaries(mapping, [
        'src/yb/util/flags/flag_tags.h',
        'src/yb/master/master-test.cc',
        'src/yb/integration-tests/upgrade-tests/ycql_only_upgrade-test.cc',
    ])
    assert selected == {
        'auto_flag_upgrade-test': 'src/yb/util/flags/flag_tags.h matches src/yb/util/flags/*',
        'basic_upgrade-test': 'src/yb/util/flags/flag_tags.h matches src/yb/**',
        'ycql_only_upgrade-test':
            'src/yb/integration-tests/upgrade-tests/ycql_only_upgrade-test.cc is its test source',
    }
    assert uts.select_binaries(mapping, ['src/yb/master/master-test.cc', 'docs/a.md']) == {}


def test_get_upgrade_tests_to_skip(tmp_path: pathlib.Path) -> None:
    root = write_mapping(tmp_path, MAPPING)
    master = 'origin/master...0123abc'
    files = ['src/yb/util/flags/flag_tags.h']
    assert uts.get_upgrade_tests_to_skip(master, files, root) == {
        'ycql_only_upgrade-test', 'xcluster_upgrade-itest'}
    assert uts.get_upgrade_tests_to_skip('origin/2025.2...0123abc', files, root) == set()
    assert uts.get_upgrade_tests_to_skip(master, files + [uts.MAPPING_PATH], root) == set()
    for mode in ['log', 'off']:
        write_mapping(tmp_path, MAPPING.replace('mode\tenforce', 'mode\t' + mode))
        assert uts.get_upgrade_tests_to_skip(master, files, root) == set()
    write_mapping(tmp_path, MAPPING.replace('mode\tenforce', 'mode\tsometimes'))
    assert uts.get_upgrade_tests_to_skip(master, files, root) == set()
    assert uts.get_upgrade_tests_to_skip(master, files, str(tmp_path / 'missing')) == set()
