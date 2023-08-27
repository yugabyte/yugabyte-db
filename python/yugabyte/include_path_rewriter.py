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

from typing import List, Set, Optional

from yugabyte.postgres_build_util import POSTGRES_BUILD_SUBDIR
from yugabyte.common_util import YB_SRC_ROOT

from yugabyte.compiler_args import get_include_path_arg
from yugabyte.file_util import assert_absolute_path


class IncludePathRewriter:
    """
    Rewrites include paths in a compilation command line so that they refer to PostgreSQL sources in
    src/postgres, not to rsynced sources in postgres_build.
    """
    original_working_directory: str
    new_working_directory: str
    already_added_include_paths: Set[str]

    # Absolute versions of include paths of the original command line. These are not rewritten
    # relative to the new working directory. We will add them at the end of the command line just
    # in case, in the same order they first appear in the original command line.
    original_abs_include_paths: List[str]

    # This contains the list of directories to add to the end of the command line if we encounter
    # the include directory <build_root>/postgres/include (containing installed PostgreSQL headers).
    additional_postgres_include_paths: List[str]
    should_add_postgres_include_paths: bool

    build_root: str
    postgres_install_dir_include_realpath: str
    new_args: List[str]

    def __init__(
            self,
            original_working_directory: str,
            new_working_directory: str,
            build_root: str) -> None:
        assert_absolute_path(original_working_directory)
        assert_absolute_path(new_working_directory)

        self.original_working_directory = original_working_directory
        self.new_working_directory = new_working_directory
        self.already_added_include_paths = set()
        self.original_abs_include_paths = []
        self.additional_postgres_include_paths = []
        self.build_root = build_root
        self.postgres_install_dir_include_realpath = os.path.realpath(
            os.path.join(self.build_root, 'postgres', 'include'))
        self.new_args = []

        postgres_build_dir = os.path.join(self.build_root, POSTGRES_BUILD_SUBDIR)
        self.additional_postgres_include_paths = [
            os.path.join(YB_SRC_ROOT, 'src', 'postgres', 'src', 'interfaces', 'libpq'),
            os.path.join(postgres_build_dir, 'src', 'include'),
            os.path.join(postgres_build_dir, 'interfaces', 'libpq')
        ]
        self.should_add_postgres_include_paths = False

    def remember_original_abs_include_path(self, original_abs_include_path: str) -> None:
        """
        Record a new absolute include path from the original command line (not rewritten relative
        to the new working directory) so that we can add it at the end of the command line.
        """
        assert_absolute_path(original_abs_include_path)
        if (original_abs_include_path not in self.already_added_include_paths and
                original_abs_include_path not in self.original_abs_include_paths):
            self.original_abs_include_paths.append(original_abs_include_path)

    def check_for_postgres_installed_include_path(self, include_path: str) -> None:
        """
        If we encounter the include directory <build_root>/postgres/include (containing installed
        PostgreSQL headers), we add some additional include directories at the end of the command
        line. This will help with finding include files even when the PostgreSQL build is not fully
        done.
        """
        assert os.path.isabs(include_path)
        if (not self.additional_postgres_include_paths and
                os.path.realpath(include_path) == self.postgres_install_dir_include_realpath):
            self.should_add_postgres_include_paths = True

    def append_include_path_arg(self, include_path: str) -> None:
        """
        Appends a new include path to the new list of arguments.
        """
        self.new_args.append(get_include_path_arg(include_path))

    def append_new_absolute_include_path(self, new_absolute_include_path: str) -> None:
        assert os.path.isabs(new_absolute_include_path)
        self.append_include_path_arg(new_absolute_include_path)

        # Prevent adding multiple aliases for the same include path.
        self.already_added_include_paths |= {
            new_absolute_include_path,
            os.path.abspath(new_absolute_include_path),
            os.path.realpath(new_absolute_include_path),
        }

    def handle_include_path(self, include_path: str) -> None:
        """
        Handles a single include path. This adds a new include path to the new list of arguments,
        and does a few bookkeeping tasks that influence the set of include paths to add at the end.
        """
        if os.path.isabs(include_path):
            # This is already an absolute path, append it as is.
            self.check_for_postgres_installed_include_path(include_path)
            self.append_new_absolute_include_path(include_path)
            return

        # This is a relative path. Find the corresponding absolute path given the working directory
        # of the compiler command.
        original_abs_include_path = os.path.realpath(
            os.path.join(self.original_working_directory, include_path))

        self.remember_original_abs_include_path(original_abs_include_path)
        self.check_for_postgres_installed_include_path(original_abs_include_path)

        # Now rewrite the path relative to the new directory where we pretend to be running the
        # compiler (i.e. where Clangd or other tools will run their equivalent of the compiler).
        new_include_path = os.path.realpath(os.path.join(self.new_working_directory, include_path))
        self.append_new_absolute_include_path(new_include_path)

    def rewrite(self, args: List[str]) -> List[str]:
        """
        Rewrites include paths in the given list of compiler arguments. Returns the updated
        list of arguments.
        """
        self.new_args = []
        self.should_add_postgres_include_paths = False

        prev_arg: Optional[str] = None
        for arg in args:
            if prev_arg == arg and arg.startswith('-I'):
                # Out of multiple consecutive identical -I flags, only handle the first one and skip
                # the rest.
                continue
            if not arg.startswith('-I'):
                self.new_args.append(arg)
                prev_arg = arg
                continue
            include_path = arg[2:]
            self.handle_include_path(include_path)
            prev_arg = arg

        if self.should_add_postgres_include_paths:
            additional_pg_include_paths = self.additional_postgres_include_paths
        else:
            additional_pg_include_paths = []

        self.new_args.extend([
            get_include_path_arg(include_path)
            for include_path in additional_pg_include_paths + self.original_abs_include_paths
        ])
        return self.new_args
