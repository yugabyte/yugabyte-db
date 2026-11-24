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

import os

from enum import Enum
from typing import Any, Set


# Tests use no files in these directories, except FILES_USED_BY_TESTS and bin/yugabyted (see
# get_file_category). Note that the "bin" directory here is the yugabyte/bin directory in the
# source tree, not the "bin" directory under the build directory, so it only has scripts and not
# yb-master / yb-tserver binaries.
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

# Files in DIRECTORIES_THAT_DO_NOT_AFFECT_TESTS that tests use. The test environment installs the
# YB Controller version from ybc_version.conf with download_ybc.sh (prep_ybc_testing in
# build-support/common-test-env.sh), and backup tests run yb_backup.py, yb-ctl and ycqlsh.
# The paths-ignore list of .github/workflows/bld-db.yml must not skip these files.
FILES_USED_BY_TESTS = {
    'bin/yb-ctl',
    'bin/ycqlsh',
    'managed/devops/bin/yb_backup.py',
    'managed/download_ybc.sh',
    'managed/src/main/resources/ybc_version.conf',
}


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
    >>> get_file_category('managed/src/main/resources/ybc_version.conf').value
    'build_scripts'
    >>> get_file_category('managed/src/main/resources/application.common.conf').value
    'does_not_affect_tests'

    """
    if os.path.isabs(rel_path):
        raise IOError("Relative path expected, got an absolute path: %s" % rel_path)
    basename = os.path.basename(rel_path)

    # Adding the check for yugabyted file before running the check for
    # DIRECTORIES_THAT_DO_NOT_AFFECT_TESTS which contains the bin directory.
    if rel_path.startswith('bin/yugabyted'):
        return SourceFileCategory.JAVA

    # No test depends on these files in the dependency graph, so rerun all tests.
    if rel_path in FILES_USED_BY_TESTS:
        return SourceFileCategory.BUILD_SCRIPTS

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
