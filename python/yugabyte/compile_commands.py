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
import logging
import json
import re
import functools

from typing import List, Dict, Any, Optional, cast, Hashable

from yugabyte.common_util import YB_SRC_ROOT
from yugabyte.json_util import read_json_file, write_json_file
from yugabyte.compiler_args import CompilerArguments

# We build PostgreSQL code in a separate directory (postgres_build) rsynced from the source tree to
# support out-of-source builds. Then, after generating the compilation commands, we rewrite them
# to work with original files (in src/postgres) so that Clangd can use them.

# We create multiple directories under $BUILD_ROOT/compile_commands and put a single file named
# compile_commands.json in each directory. This is because many Clang-based tools take a directory
# path containing compile_commands.json as a parameter, so the compilation database file should
# always be named compile_commands.json.

# The directory names listed below vary across two dimensions:
# - YB. vs. PostgreSQL vs. combined.
# - Raw vs. postprocessed. "Postprocessed" means paths are rewritten to refer to files in the source
#   directory rather than files copied to the postgres_build directory.

COMPILATION_DATABASE_SUBDIR_NAME = 'compile_commands'

COMBINED_POSTPROCESSED_DIR_NAME = 'combined_postprocessed'
PG_POSTPROCESSED_DIR_NAME = 'pg_postprocessed'
YB_POSTPROCESSED_DIR_NAME = 'yb_postprocessed'

COMBINED_RAW_DIR_NAME = 'combined_raw'
YB_RAW_DIR_NAME = 'yb_raw'
PG_RAW_DIR_NAME = 'pg_raw'
YB_SRC_ROOT_SLASH = YB_SRC_ROOT + '/'


def create_compile_commands_symlink(actual_file_path: str) -> None:
    new_link_path = os.path.join(YB_SRC_ROOT, 'compile_commands.json')

    if (not os.path.exists(new_link_path) or
            not os.path.islink(new_link_path) or
            os.path.realpath(new_link_path) != os.path.realpath(actual_file_path)):

        # os.path.exists may return false if the link exists and points to a nonexistent file.
        if os.path.exists(new_link_path) or os.path.islink(new_link_path):
            logging.info("Removing the old file/link at %s", new_link_path)
            os.remove(new_link_path)

        where_link_points = os.path.relpath(
            os.path.realpath(actual_file_path),
            os.path.realpath(os.path.dirname(new_link_path)))
        try:
            os.symlink(where_link_points, new_link_path)
        except Exception as e:
            logging.exception(
                f"Error creating a symbolic link pointing to {where_link_points} "
                f"named {new_link_path}.")
            raise e
        logging.info(f"Created symlink at {new_link_path} pointing to {where_link_points}")


def get_arguments_from_compile_command_item(compile_command_item: Dict[str, Any]) -> List[str]:
    if 'command' in compile_command_item:
        if 'arguments' in compile_command_item:
            raise ValueError(
                "Invalid compile command item: %s (both 'command' and 'arguments' are "
                "present)" % json.dumps(compile_command_item))
        arguments = compile_command_item['command'].split()
    elif 'arguments' in compile_command_item:
        arguments = compile_command_item['arguments']
    else:
        raise ValueError(
            "Invalid compile command item: %s (neither 'command' nor 'arguments' are present)" %
            json.dumps(compile_command_item))
    return arguments


def filter_compile_commands(input_path: str, output_path: str, file_name_regex_str: str) -> None:
    compiled_re = re.compile(file_name_regex_str)
    input_cmds = read_json_file(input_path)
    output_cmds = [
        item for item in input_cmds
        if compiled_re.match(os.path.basename(item['file']))
    ]
    logging.info(
        "Filtered compilation commands from %d to %d entries using the regex %s",
        len(input_cmds), len(output_cmds), file_name_regex_str)
    write_json_file(
        output_cmds, output_path,
        description_for_log="filtered compilation commands file")


def get_compile_commands_file_path(
        build_root: str,
        subdir_name: Optional[str] = None) -> str:
    dir_path = os.path.join(build_root, COMPILATION_DATABASE_SUBDIR_NAME)
    if subdir_name is not None:
        dir_path = os.path.join(dir_path, subdir_name)
    return os.path.join(dir_path, 'compile_commands.json')


@functools.total_ordering
class CompileCommand:
    """
    Used for compilation commands that have already been put into our final format with
    """
    file_path: str
    dir_path: str
    compiler_args: CompilerArguments

    def __init__(
            self,
            file_path: str,
            dir_path: str,
            args: List[str]) -> None:
        if not file_path.startswith(YB_SRC_ROOT_SLASH):
            raise ValueError(
                "File path in a compilation command does not starts with "
                f"YB_SRC_ROOT followed by forward slash ({YB_SRC_ROOT_SLASH}): {self.file_path}")

        # Even though we expect the file path here to be absolute, we still call os.path.abspath
        # on it because we can get paths with double dots in them such as:
        # $YB_SRC_ROOT/src/postgres/src/backend/../../src/timezone/pgtz.c
        self.file_path = os.path.abspath(file_path)

        self.dir_path = dir_path
        self.compiler_args = CompilerArguments(args)

        output_path = self.compiler_args.get_output_path()
        if not os.path.isabs(output_path):
            self.compiler_args.set_output_path(os.path.abspath(
                os.path.join(self.dir_path, output_path)))

    @property
    def rel_file_path(self) -> str:
        assert self.file_path.startswith(YB_SRC_ROOT_SLASH)
        return self.file_path[len(YB_SRC_ROOT_SLASH):]

    @staticmethod
    def from_json_obj(json_dict: Dict[str, Any]) -> 'CompileCommand':
        return CompileCommand(
            file_path=json_dict['file'],
            dir_path=json_dict['directory'],
            args=json_dict['arguments']
        )

    def as_json_obj(self) -> Dict[str, Any]:
        return {
            'file': self.file_path,
            'directory': self.dir_path,
            'arguments': self.compiler_args.args
        }

    def __repr__(self) -> str:
        return 'CompileCommand(file_path=%s, dir_path=%s, compiler_args=%s)' % (
            self.file_path, self.dir_path, self.compiler_args.args)

    __str__ = __repr__

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, CompileCommand):
            return False
        return (
            self.file_path == other.file_path and
            self.dir_path == other.dir_path and
            self.compiler_args == other.compiler_args
        )

    def __ne__(self, other: Any) -> bool:
        return not self.__eq__(other)

    def __hash__(self) -> int:
        return hash((self.file_path, self.dir_path, self.compiler_args))

    def __lt__(self, other: Any) -> bool:
        return (
            self.file_path, self.dir_path, self.compiler_args
        ) < (
            other.file_path, other.dir_path, other.compiler_args
        )

    def equivalence_key(self) -> Hashable:
        return (self.file_path, self.dir_path, self.compiler_args.equivalence_key())
