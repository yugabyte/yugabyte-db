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

"""
Maps a PR's changed files to the ybbld build targets that must run for it.

Reads changed paths from stdin, one per line, and prints a JSON list of targets, e.g. ["DB"].

The bld workflow is triggered by PR comments, and GitHub's paths / paths-ignore filters do not
apply to issue_comment events, so the routing lives here. Patterns use the same syntax and
evaluation as those filters: '*' does not cross '/', '**' does, a leading '!' re-includes a path
an earlier pattern matched, and the last matching pattern wins.
"""

import json
import re
import sys
from typing import Iterable, List, NamedTuple, Pattern, Tuple


class Target(NamedTuple):
    name: str
    # True: build when any changed file is NOT matched (GitHub's paths-ignore).
    # False: build when any changed file is matched (GitHub's paths).
    ignore: bool
    patterns: Tuple[str, ...]


# Every path must select at least one target, or the PR never gets a yb-required status and cannot
# merge. So anything added to DB's ignore list must be covered by one of the other targets.
TARGETS = (
    Target('DB', ignore=True, patterns=(
        '.agents/**',
        '.arc*',
        '.claude/**',
        '.cursor/**',
        '.git/**',
        '.github/**',
        '.gitignore',
        'architecture/**',
        'arcanist/**',
        'CONTRIBUTORS.md',
        'cloud/**',
        'code_style',
        'community/**',
        'java/yb-client',
        'docs/**',
        'LICENSE.md',
        'license/**',
        'managed/**',
        '!managed/src/main/resources/ybc_version.conf',
        'NOTICE.txt',
        '**/README*',
        'sample/**',
        'src/lint/**',
    )),
    Target('NO-BLD', ignore=False, patterns=(
        '.agents/**',
        '.arc*',
        '.claude/**',
        '.cursor/**',
        '.git/**',
        '.github/**',
        '.gitignore',
        'architecture/**',
        'arcanist/**',
        'CONTRIBUTORS.md',
        'cloud/**',
        'code_style',
        'community/**',
        'java/yb-client',
        'LICENSE.md',
        'license/**',
        'NOTICE.txt',
        '**/README*',
        'sample/**',
        'src/lint/**',
    )),
    Target('DOCS', ignore=False, patterns=(
        'docs/**',
        'README.md',
    )),
    Target('YBA', ignore=False, patterns=(
        'managed/**',
        '!managed/ui/**',
        '!managed/yba-installer/**',
    )),
    Target('YBA-CLI', ignore=False, patterns=(
        'managed/yba-cli/**',
    )),
    Target('YBA-INSTALL', ignore=False, patterns=(
        'managed/yba-installer/**',
        'managed/devops/bin/yb_platform_backup.sh',
    )),
    Target('YBA-UI', ignore=False, patterns=(
        'managed/ui/**',
    )),
)


def glob_to_regex(glob: str) -> Pattern[str]:
    """Compiles one GitHub filter pattern (without its '!' prefix)."""
    if re.search(r'[?+\[\]]', glob):
        # GitHub gives these characters meanings this translation does not implement.
        raise ValueError(f'Unsupported character in filter pattern: {glob}')
    out = []
    i = 0
    while i < len(glob):
        if glob.startswith('**/', i):
            out.append('(?:.*/)?')
            i += 3
        elif glob.startswith('**', i):
            out.append('.*')
            i += 2
        elif glob[i] == '*':
            out.append('[^/]*')
            i += 1
        else:
            out.append(re.escape(glob[i]))
            i += 1
    return re.compile(''.join(out) + r'\Z')


def _compile(patterns: Iterable[str]) -> List[Tuple[bool, Pattern[str]]]:
    return [(p.startswith('!'), glob_to_regex(p.lstrip('!'))) for p in patterns]


_COMPILED = [(t, _compile(t.patterns)) for t in TARGETS]


def _matches(path: str, compiled: List[Tuple[bool, Pattern[str]]]) -> bool:
    matched = False
    for negated, regex in compiled:
        if regex.match(path):
            matched = not negated
    return matched


def targets_for(paths: Iterable[str]) -> List[str]:
    paths = [p for p in paths if p]
    return [
        target.name for target, compiled in _COMPILED
        if any(_matches(p, compiled) != target.ignore for p in paths)
    ]


def main() -> None:
    print(json.dumps(targets_for(line.strip() for line in sys.stdin)))


if __name__ == '__main__':
    main()
