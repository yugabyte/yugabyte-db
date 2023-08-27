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

"""
A command-line tool for performing various actions based on the compilation commands database.
"""

import argparse
import copy
import fnmatch
import json
import logging
import multiprocessing
import os
import random
import re
import sys
import copy
import difflib
import subprocess

from collections import defaultdict
from typing import Callable, Dict, List, Any, Optional, Set, Union, Tuple, DefaultDict

from overrides import overrides  # type: ignore

from yugabyte.tool_base import YbBuildToolBase, UserFriendlyToolError
from yugabyte.compile_commands import (
    get_compile_commands_file_path,
    CompileCommand,
    COMBINED_POSTPROCESSED_DIR_NAME,
)
from yugabyte.common_util import YB_SRC_ROOT, ensure_enclosed_by
from yugabyte.json_util import read_json_file

from yugabyte.command_util import shlex_join
from yugabyte.clang_tidy_runner import ClangTidyRunner
from yugabyte.compiler_parallel_runner import CompilerParallelRunner
from yugabyte.preprocessor import run_preprocessor


# We create methods with this suffix in the tool class that implement various subcommands.
CMD_IMPL_METHOD_SUFFIX = '_cmd_impl'


def is_protobuf_generated_path(file_path: str) -> bool:
    return file_path.endswith((
        '.pb.cc',
        '.messages.cc',
        '.service.cc',
        '.proxy.cc',
    ))


def fix_compiler_path(args: List[str], toolchain_dir: Optional[str]) -> None:
    """
    Fixes compiler path in case it points to a nonexistent location. Modifies args in place.
    """
    compiler_path = args[0]
    is_wrapper = os.path.basename(os.path.dirname(compiler_path)) == 'compiler-wrappers'
    if is_wrapper or not os.path.exists(compiler_path):
        compiler_base_name = os.path.basename(compiler_path)
        if compiler_base_name == 'cc':
            new_compiler_base_name = 'clang'
        elif compiler_base_name == 'c++':
            new_compiler_base_name = 'clang++'
        else:
            raise ValueError("Unexpected compiler base name: " + compiler_base_name)

        assert toolchain_dir is not None
        args[0] = os.path.join(toolchain_dir, 'bin', new_compiler_base_name)


class CompileCommandsTool(YbBuildToolBase):
    compile_commands_path: str
    compile_commands: List[Dict[str, Any]]
    parsed_commands: List[CompileCommand]
    subcommands_added: Set[str]
    llvm_toolchain_dir: Optional[str]

    def __init__(self) -> None:
        super().__init__()
        self.subcommands_added = set()

    def filter_compile_commands_by_rel_file_path(
            self,
            predicate: Callable[[str], bool],
            predicate_description: str) -> None:
        """
        Filters the compile commands stored in self.compile_commands by the relative file path,
        according to the given predicate function. Stores the filtered commands back to the
        self.compile_commands field. Prints a message to the log if any commands were filtered out.

        :param predicate: A function that takes a relative file path and returns True if the
                          command should be kept, False if it should be filtered out.
        :param predicate_description: A description of the predicate, for logging purposes.
        """
        filtered_commands = []
        for compile_command in self.compile_commands:
            parsed_cmd = CompileCommand.from_json_obj(compile_command)
            if predicate(parsed_cmd.rel_file_path):
                filtered_commands.append(compile_command)
        if len(self.compile_commands) != len(filtered_commands):
            logging.info(
                "Filtered %d compilation commands down to %d using predicate: %s",
                len(self.compile_commands), len(filtered_commands), predicate_description)
            self.compile_commands = filtered_commands

    def filter_commands(self) -> None:
        """
        Filters the compilation commands according to the command line arguments.
        """
        if self.args.glob:
            glob_pattern_str = ensure_enclosed_by(self.args.glob, '*', '*')
            self.filter_compile_commands_by_rel_file_path(
                lambda rel_file_path: fnmatch.fnmatch(rel_file_path, glob_pattern_str),
                f"glob pattern {glob_pattern_str}")
        if self.args.regex:
            regex_str = ensure_enclosed_by(self.args.regex, '^', '$')
            matcher = re.compile(regex_str)
            self.filter_compile_commands_by_rel_file_path(
                lambda rel_file_path: matcher.match(rel_file_path) is not None,
                f"regular expression {regex_str}"
            )
        if not self.compile_commands:
            raise UserFriendlyToolError("No matching compilation commands found.")
        if self.args.single_file and len(self.compile_commands) != 1:
            rel_paths_found = sorted([
                CompileCommand.from_json_obj(cmd).rel_file_path for cmd in self.compile_commands
            ])
            logging.warning(
                "Relative paths of files corresponding to compile commands found:\n    " +
                "\n    ".join(rel_paths_found))
            raise UserFriendlyToolError(
                "The --single-file option enforces that we choose exactly one compilation "
                f"command but found {len(self.compile_commands)} files. Use --glob or --regex "
                "flags to filter the set of files to work with to a single file.")
        if len(self.compile_commands) == 1:
            # We are effectively using the single-file mode.
            self.args.single_file = True

    @staticmethod
    def get_impl_fn_name_by_subcommand(subcommand: str) -> str:
        """
        >>> CompileCommandsTool.get_impl_fn_name_by_subcommand('foo_bar')
        'foo_bar_cmd_impl'
        """
        return subcommand.replace('-', '_') + CMD_IMPL_METHOD_SUFFIX

    @staticmethod
    def get_subcommand_by_impl_fn_name(impl_fn_name: str) -> str:
        """
        >>> CompileCommandsTool.get_subcommand_by_impl_fn_name('foo_bar_cmd_impl')
        'foo-bar'
        """
        if not impl_fn_name.endswith(CMD_IMPL_METHOD_SUFFIX):
            raise ValueError("Invalid implementation function name: " + impl_fn_name)
        return impl_fn_name[:-len(CMD_IMPL_METHOD_SUFFIX)].replace('_', '-')

    # ---------------------------------------------------------------------------------------------
    # Command line processing
    # ---------------------------------------------------------------------------------------------

    def add_subcommand(self, subcommand_or_fn: Union[str, Callable]) -> argparse.ArgumentParser:
        subcommand: str
        if isinstance(subcommand_or_fn, str):
            subcommand = subcommand_or_fn
        else:
            subcommand = CompileCommandsTool.get_subcommand_by_impl_fn_name(
                subcommand_or_fn.__name__)

        if subcommand in self.subcommands_added:
            raise ValueError("Cannot add subcommand twice: %s" % subcommand)
        self.subcommands_added.add(subcommand)

        impl_fn_name = self.get_impl_fn_name_by_subcommand(subcommand)
        doc_str = getattr(self.__class__, impl_fn_name).__doc__
        if doc_str is None or not doc_str.strip():
            raise ValueError("No docstring provided for method " + impl_fn_name)
        return self.subparsers.add_parser(
            subcommand,
            help=doc_str
        )

    # ---------------------------------------------------------------------------------------------
    # Overrides of YbBuildToolBase methods
    # ---------------------------------------------------------------------------------------------

    @overrides
    def get_default_build_root(self) -> Optional[str]:
        compile_commands_link_path = os.path.join(YB_SRC_ROOT, 'compile_commands.json')
        if os.path.islink(compile_commands_link_path):
            link_points_to = os.readlink(compile_commands_link_path)
            if link_points_to.startswith('build/'):
                link_items = link_points_to.split('/')
                candidate_build_root = os.path.join(
                    YB_SRC_ROOT,
                    link_items[0],
                    link_items[1]
                )
                if os.path.isdir(candidate_build_root):
                    return candidate_build_root

        build_parent_dir = os.path.join(YB_SRC_ROOT, 'build')
        candidates = []
        for build_subdir in os.listdir(build_parent_dir):
            if os.path.islink(build_subdir):
                continue
            if build_subdir.startswith('compilecmds-'):
                candidates.append(build_subdir)
        if len(candidates) == 1:
            return os.path.join(build_parent_dir, candidates[0])
        return None

    @overrides
    def add_command_line_args(self) -> None:
        common_pattern_help = (
            'The pattern is only applied to the part of file path relative to the YugabyteDB '
            'source directory.'
        )
        self.arg_parser.add_argument(
            '--glob', '-g',
            help='Only process files with the given glob pattern. The glob pattern is not '
                 'anchored on either end, i.e. it is as if it has a "*" at both ends. If you need '
                 'more precise matching, use the --regex option. ' + common_pattern_help
        )
        self.arg_parser.add_argument(
            '--regex', '-r',
            help='Only process files with the given regex pattern. ' + common_pattern_help + '. '
                 'We will autaomatically add "^" and "$" to the beginning and end of the pattern.'
        )

        self.arg_parser.add_argument(
            '--single-file', '-s',
            action='store_true',
            help='Only work on a single file. Print an error message and exit if it is not '
                 'possible to unambgiously decide what file to work with.'
        )

        num_cpus = multiprocessing.cpu_count()
        self.arg_parser.add_argument(
            '--parallelism', '-j',
            help='Level of parallelism. Default: %d' % num_cpus,
            default=num_cpus
        )

        self.subparsers = self.arg_parser.add_subparsers(
            title='subcommands',
            dest='subcommand')

        clang_tidy_parser = self.add_subcommand(self.clang_tidy_cmd_impl)
        clang_tidy_parser.add_argument(
            'clang_tidy_extra_args',
            help='Extra arguments to pass to clang-tidy',
            nargs=argparse.REMAINDER)
        clang_tidy_parser.add_argument(
            '--apply-fixes',
            help='Apply fixes suggested by clang-tidy. Check if the resulting files could '
                 'be successfully compiled. If the compilation fails, revert the changes.',
            action='store_true')
        clang_tidy_parser.add_argument(
            '--list-checks',
            help='List available clang-tidy checks',
            action='store_true')
        clang_tidy_parser.add_argument(
            '--checks',
            help='Specify clang-tidy checks')
        clang_tidy_parser.add_argument(
            '--clang-tidy-help',
            help='Invoke clang-tidy with --help. Do not do parallel processing of files.')

        # Add the remaining subcommands, which are assumed to have no custom parameters.
        for field_name in dir(self.__class__):
            if field_name.endswith(CMD_IMPL_METHOD_SUFFIX):
                subcommand = self.get_subcommand_by_impl_fn_name(field_name)
                if subcommand not in self.subcommands_added:
                    self.add_subcommand(subcommand)

    @overrides
    def validate_and_process_args(self) -> None:
        super().validate_and_process_args()
        if self.args.subcommand == 'preprocess':
            self.args.single_file = True

        self.llvm_toolchain_dir = os.path.realpath(
            os.path.join(self.args.build_root, 'toolchain'))

    @overrides
    def run_impl(self) -> None:
        self.compile_commands_path = get_compile_commands_file_path(
            self.args.build_root, COMBINED_POSTPROCESSED_DIR_NAME)
        self.compile_commands = read_json_file(self.compile_commands_path)
        subcommand = self.args.subcommand
        if subcommand is None:
            logging.error("Subcommand not specified.")
            self.arg_parser.print_help()
            sys.exit(1)

        self.filter_commands()
        self.parsed_commands = sorted(
            [CompileCommand.from_json_obj(cmd) for cmd in self.compile_commands],
            key=lambda cmd: cmd.rel_file_path
        )
        for parsed_command in self.parsed_commands:
            fix_compiler_path(
                parsed_command.compiler_args.mutable_arg_list(), self.llvm_toolchain_dir)

        getattr(self, self.get_impl_fn_name_by_subcommand(subcommand))()

    # ---------------------------------------------------------------------------------------------
    # Command implementations
    # ---------------------------------------------------------------------------------------------

    def stats_cmd_impl(self) -> None:
        """
        Show statistics about the files for which compilation commands are present.
        """
        num_cmds = 0
        num_in_src_dir = 0
        num_in_ent_src_dir = 0
        num_in_build_dir = 0
        num_protobuf_generated = 0
        num_in_postgres_build = 0
        num_other_in_build_dir = 0

        src_dir = os.path.join(YB_SRC_ROOT, 'src')
        ent_src_dir = os.path.join(YB_SRC_ROOT, 'ent', 'src')

        paths_rel_to_build_root: Set[str] = set()
        paths_rel_to_postgres_build_dir: Set[str] = set()

        for compile_command in self.compile_commands:
            num_cmds += 1
            file_path = compile_command['file']
            assert os.path.isabs(file_path)
            if file_path.startswith(src_dir + '/'):
                num_in_src_dir += 1
            if file_path.startswith(ent_src_dir + '/'):
                num_in_ent_src_dir += 1
            if file_path.startswith(self.args.build_root + '/'):
                num_in_build_dir += 1
                path_rel_to_build_root = file_path[len(self.args.build_root) + 1:]
                paths_rel_to_build_root.add(path_rel_to_build_root)
                if path_rel_to_build_root.startswith('postgres_build/'):
                    path_rel_to_postgres_build = path_rel_to_build_root[len('postgres_build/'):]
                    paths_rel_to_postgres_build_dir.add(path_rel_to_postgres_build)
                    num_in_postgres_build += 1
                    p = os.path.join(YB_SRC_ROOT, 'src', 'postgres', path_rel_to_postgres_build)
                elif is_protobuf_generated_path(file_path):
                    num_protobuf_generated += 1
                else:
                    num_other_in_build_dir += 1

        print(f"Total files with compilation commands: {num_cmds}")
        print(f"Files in the src directory: {num_in_src_dir}")
        print(f"Files in the ent/src directory: {num_in_ent_src_dir}")
        print(f"Files in the build directory: {num_in_build_dir}")
        print(f"Files in the postgres_build directory: {num_in_postgres_build}")
        print(f"Protobuf-generated files: {num_protobuf_generated}")
        print(f"Other files in build directory: {num_other_in_build_dir}")

    def show_cmds_cmd_impl(self) -> None:
        """
        Show compilation commands.
        """
        for compile_command in self.compile_commands:
            print(json.dumps(compile_command, indent=2))

    def preprocess_cmd_impl(self) -> None:
        """
        Preprocess a single translation unit and output the result to stdout.
        """
        if not self.args.single_file:
            raise ValueError("The preprocess command only works with a single file")
        run_preprocessor(self.parsed_commands[0], error_ok=False)

    def list_files_cmd_impl(self) -> None:
        """
        List source files present on the compilation database that match the given glob or regex.
        """
        for cmd in self.parsed_commands:
            print(cmd.rel_file_path)

    def clang_tidy_cmd_impl(self) -> None:
        """
        Runs clang-tidy.
        """
        assert self.llvm_toolchain_dir is not None

        clang_tidy_path = os.path.join(self.llvm_toolchain_dir, 'bin', 'clang-tidy')

        extra_args = list(self.args.clang_tidy_extra_args)
        if extra_args and extra_args[0] == '--':
            extra_args = extra_args[1:]

        checks = self.args.checks
        if self.args.list_checks and checks is None:
            checks = '*'

        if checks is not None:
            extra_args.append('--checks=' + checks)

        single_clang_tidy_command = False
        if self.args.list_checks:
            single_clang_tidy_command = True
            extra_args.append('--list-checks')
        if self.args.clang_tidy_help:
            single_clang_tidy_command = True
            extra_args.append('--help')
        if '--help' in extra_args:
            single_clang_tidy_command = True

        if single_clang_tidy_command:
            clang_tidy_cmd = [clang_tidy_path] + extra_args
            logging.info("Running clang-tidy: %s", shlex_join(clang_tidy_cmd))
            subprocess.check_call(clang_tidy_cmd)
            return

        clang_tidy_runner = ClangTidyRunner(
            parallelism=self.args.parallelism,
            clang_tidy_path=clang_tidy_path,
            compile_commands_path=self.compile_commands_path,
            extra_args=extra_args,
            apply_fixes=self.args.apply_fixes)

        # Do not use clang-tidy for auto-generated files in the build directory.
        input_cmds = [
            cmd for cmd in self.parsed_commands
            if not cmd.rel_file_path.startswith('build/')
        ]
        random.shuffle(input_cmds)

        clang_tidy_runner.run_tasks(input_cmds)

    def analyze_duplicates_cmd_impl(self) -> None:
        """
        Analyze duplicates in the compilation database.
        """
        items_by_abs_path: DefaultDict[str, List[CompileCommand]] = defaultdict(list)
        for cmd in self.parsed_commands:
            cmd = copy.deepcopy(cmd)
            items_by_abs_path[
                os.path.abspath(os.path.join(YB_SRC_ROOT, cmd.rel_file_path))
            ].append(cmd)
        num_items_with_duplicates = 0
        for abs_path, items in items_by_abs_path.items():
            if len(items) == 1:
                continue
            num_items_with_duplicates += 1
            all_items_equal = True
            all_args_equal = True
            all_args_equal_length = True
            for i in range(len(items) - 1):
                if items[i] != items[i + 1]:
                    all_items_equal = False
                if items[i].compiler_args != items[i + 1].compiler_args:
                    all_args_equal = False
                if len(items[i].compiler_args.args) != len(items[i + 1].compiler_args.args):
                    all_args_equal_length = False

            if not all_items_equal:
                print(f"{abs_path}:")
                for item in items:
                    print(f"    {item.rel_file_path}")
                    print(f"    {item.dir_path}")
                if len(items) == 2:
                    print('=' * 80)
                    print('\n'.join(difflib.unified_diff(
                        items[0].compiler_args.args,
                        items[1].compiler_args.args
                    )))
                    print('=' * 80)

                print(f"    all_args_equal={all_args_equal}")
                print(f"    all_args_equal_length={all_args_equal_length}")
        logging.info("Found %d file paths with duplicate compilation commands",
                     num_items_with_duplicates)

    def compile_cmd_impl(self) -> None:
        """
        Run the compiler on the given compilation commands, discarding the output. This is useful
        for checking if the compilation commands are valid.
        """
        clang_tidy_runner = CompilerParallelRunner(
            parallelism=self.args.parallelism,
            compile_commands_path=self.compile_commands_path,
            extra_args=[])

        input_cmds = list(self.parsed_commands)
        random.shuffle(input_cmds)

        clang_tidy_runner.run_tasks(input_cmds)

    # ---------------------------------------------------------------------------------------------
    # End of command implementations
    # ---------------------------------------------------------------------------------------------


def main() -> None:
    ccmd_tool = CompileCommandsTool()
    ccmd_tool.run()


if __name__ == '__main__':
    main()
