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

"""Picks the upgrade test binaries a master diff build skips, from the upgrade test mapping."""

import fnmatch
import logging
import os

from typing import Dict, List, NamedTuple, Optional, Set, Tuple


MAPPING_PATH = 'build-support/upgrade_test_mapping.txt'
UPGRADE_TEST_DIR = 'src/yb/integration-tests/upgrade-tests/'
MODES = ('off', 'log', 'enforce')
# A change to any of these runs every upgrade test.
SELF_PATHS = (
    MAPPING_PATH,
    'python/yugabyte/upgrade_test_selection.py',
    'python/yugabyte/dependency_graph.py',
    'python/yugabyte/run_tests_on_spark.py',
)


class UpgradeTestMapping(NamedTuple):
    mode: str
    rules: List[Tuple[str, str]]
    ignore: List[str]


def check_glob(glob: str) -> None:
    """Raises ValueError unless glob is a clean path glob relative to the repository root."""
    if (not glob or glob.strip() != glob or glob.startswith('/') or glob.endswith('/') or
            '//' in glob):
        raise ValueError('%r is not a repository-relative path glob' % glob)
    for element in glob.split('/'):
        if element in ('.', '..') or (element != '**' and '**' in element):
            raise ValueError('%r: %r is not a path element glob' % (glob, element))


def parse_mapping(text: str) -> UpgradeTestMapping:
    """Parses the mapping file's text; raises ValueError on a malformed line."""
    mode = ''
    rules: List[Tuple[str, str]] = []
    ignore: List[str] = []
    seen: Set[Tuple[str, str]] = set()
    for line_num, raw_line in enumerate(text.split('\n'), 1):
        line = raw_line.rstrip('\r')
        if not line.strip() or line.strip().startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) != 2 or not fields[0]:
            raise ValueError('line %d: want two tab-separated columns: %r' % (line_num, line))
        key, value = fields
        if key == 'mode':
            if mode or value not in MODES:
                raise ValueError('line %d: want one mode line, with one of %s' % (
                    line_num, ', '.join(MODES)))
            mode = value
            continue
        check_glob(value)
        if key == '-':
            ignore.append(value)
            continue
        if '/' in key or ' ' in key or (key, value) in seen:
            raise ValueError('line %d: bad or repeated rule: %r' % (line_num, line))
        seen.add((key, value))
        rules.append((key, value))
    if not mode:
        raise ValueError('no mode line')
    return UpgradeTestMapping(mode, rules, ignore)


def match_glob(glob: str, path: str) -> bool:
    """
    Whether path matches glob: ** spans any number of path elements, * and ? match within one.

    >>> match_glob('src/yb/**', 'src/yb/master/master.cc')
    True
    >>> match_glob('src/yb/master/*', 'src/yb/master/sub/a.cc')
    False
    >>> match_glob('src/yb/**/*auto_flags_manager*', 'src/yb/server/auto_flags_manager_base.cc')
    True
    """
    return _match_elements(glob.split('/'), path.split('/'))


def _match_elements(glob: List[str], path: List[str]) -> bool:
    if not glob:
        return not path
    if glob[0] == '**':
        return any(_match_elements(glob[1:], path[i:]) for i in range(len(path) + 1))
    return (bool(path) and fnmatch.fnmatchcase(path[0], glob[0]) and
            _match_elements(glob[1:], path[1:]))


def select_binaries(mapping: UpgradeTestMapping, changed_files: List[str]) -> Dict[str, str]:
    """Maps each binary the changed files select to the reason it was selected."""
    binaries_by_glob: Dict[str, List[str]] = {}
    for binary, glob in mapping.rules:
        binaries_by_glob.setdefault(glob, []).append(binary)
    all_binaries = {binary for binary, _ in mapping.rules}
    selected: Dict[str, str] = {}
    for path in sorted(changed_files):
        if path.startswith(UPGRADE_TEST_DIR):
            own = os.path.basename(path)[:-len('.cc')] if path.endswith('.cc') else ''
            if own in all_binaries and own not in selected:
                selected[own] = '%s is its test source' % path
        if any(match_glob(glob, path) for glob in mapping.ignore):
            continue
        for glob, binaries in binaries_by_glob.items():
            if all(b in selected for b in binaries) or not match_glob(glob, path):
                continue
            for binary in binaries:
                selected.setdefault(binary, '%s matches %s' % (path, glob))
        if len(selected) == len(all_binaries):
            break
    return selected


def base_branch(git_diff: str) -> Optional[str]:
    """
    The branch a three-dot git diff range compares the change against.

    >>> base_branch('origin/master...0123abc')
    'master'
    >>> print(base_branch('HEAD~1'))
    None
    """
    if '...' not in git_diff:
        return None
    base = git_diff.split('...', 1)[0]
    return base[len('origin/'):] if base.startswith('origin/') else base


def get_upgrade_tests_to_skip(
        git_diff: str, changed_files: List[str], yb_src_root: str) -> Set[str]:
    """Logs what the mapping selects for a change; returns the binaries to skip in enforce mode."""
    branch = base_branch(git_diff)
    if branch != 'master':
        logging.info("Upgrade test mapping: not applied, the change is against %s", branch)
        return set()
    touched = sorted(set(changed_files) & set(SELF_PATHS))
    if touched:
        logging.info("Upgrade test mapping: not applied, the change touches %s",
                     ', '.join(touched))
        return set()
    try:
        with open(os.path.join(yb_src_root, MAPPING_PATH)) as mapping_file:
            mapping = parse_mapping(mapping_file.read())
        if mapping.mode == 'off':
            logging.info("Upgrade test mapping: mode off, every upgrade test runs")
            return set()
        selected = select_binaries(mapping, changed_files)
    except Exception as ex:
        logging.warning("Upgrade test mapping: not applied, every upgrade test runs: %s", ex)
        return set()
    for binary in sorted(selected):
        logging.info("Upgrade test mapping: runs %s: %s", binary, selected[binary])
    mapped = {binary for binary, _ in mapping.rules}
    skipped = mapped - set(selected)
    logging.info("Upgrade test mapping (mode %s): %s %d of %d mapped binaries: %s",
                 mapping.mode, 'skips' if mapping.mode == 'enforce' else 'would skip',
                 len(skipped), len(mapped), ', '.join(sorted(skipped)))
    return skipped if mapping.mode == 'enforce' else set()
