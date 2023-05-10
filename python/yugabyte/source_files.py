# Copyright (c) Yugabyte, Inc.
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

import os

from enum import Enum
from typing import Any, Set


# As of August 2019, there is nothing in the "bin", "managed" and "www" directories that
# is being used by tests.
# If that changes, this needs to be updated. Note that the "bin" directory here is the
# yugabyte/bin directory in the source tree, not the "bin" directory under the build
# directory, so it only has scripts and not yb-master / yb-tserver binaries.
DIRECTORIES_THAT_DO_NOT_AFFECT_TESTS = [
    'architecture',
    'bin',
    'cloud',
    'community',
    'docs',
    'managed',
    'sample',
    'www',
]


class SourceFileCategory(Enum):
    """
    We classify source files in code changes into these categories.
    """

    CMAKE = 'cmake'
    POSTGRES = 'postgres'
    CPP = 'c++'
    JAVA = 'java'
    PYTHON = 'python'
    DOES_NOT_AFFECT_TESTS = 'does_not_affect_tests'
    BUILD_SCRIPTS = 'build_scripts'
    OTHER = 'other'

    def __str__(self) -> str:
        return self.value

    def __lt__(self, other: Any) -> bool:
        assert isinstance(other, SourceFileCategory)
        return self.value < other.value

    def __eq__(self, other: Any) -> bool:
        assert isinstance(other, SourceFileCategory)
        return self.value == other.value

    def __hash__(self) -> int:
        return hash(self.value)


# File changes in any category other than these will cause all tests to be re-run.  Even though
# changes to Python code affect the test framework, we can consider this Python code to be
# reasonably tested already, by running doctests, this script, the packaging script, etc.  We can
# remove "python" from this whitelist in the future.
CATEGORIES_NOT_CAUSING_RERUN_OF_ALL_TESTS: Set[SourceFileCategory] = {
    SourceFileCategory.JAVA,
    SourceFileCategory.CPP,
    SourceFileCategory.PYTHON,
    SourceFileCategory.DOES_NOT_AFFECT_TESTS
}


def get_file_category(rel_path: str) -> SourceFileCategory:
    """
    Categorize file changes so that we can decide what tests to run.

    @param rel_path: path relative to the source root (not to the build root)

    >>> get_file_category('src/postgres/src/backend/executor/execScan.c').value
    'postgres'

    """
    if os.path.isabs(rel_path):
        raise IOError("Relative path expected, got an absolute path: %s" % rel_path)
    basename = os.path.basename(rel_path)

    if rel_path.split(os.sep)[0] in DIRECTORIES_THAT_DO_NOT_AFFECT_TESTS:
        return SourceFileCategory.DOES_NOT_AFFECT_TESTS

    if rel_path == 'yb_build.sh':
        # The main build script is being run anyway, so we hope that most issues will come out at
        # that stage.
        return SourceFileCategory.DOES_NOT_AFFECT_TESTS

    if basename == 'CMakeLists.txt' or basename.endswith('.cmake'):
        return SourceFileCategory.CMAKE

    if rel_path.startswith('src/postgres'):
        return SourceFileCategory.POSTGRES

    if rel_path.startswith('src/'):
        return SourceFileCategory.CPP

    if rel_path.startswith('build-support/') or rel_path.startswith('python/yugabyte/'):
        return SourceFileCategory.BUILD_SCRIPTS

    if rel_path.startswith('python/'):
        return SourceFileCategory.PYTHON

    if rel_path.startswith('java/'):
        return SourceFileCategory.JAVA

    return SourceFileCategory.OTHER
