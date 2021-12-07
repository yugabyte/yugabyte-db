# Copyright (c) Yugabyte, Inc.

import os
import sys
import logging
import json
import re
import subprocess

from typing import List, Dict, Set, Union, Any, Optional

# TODO: add types to yugabyte_pycommon and remove "type: ignore" here.
from yugabyte_pycommon import WorkDirContext  # type: ignore

from yb.common_util import (
    YB_SRC_ROOT,
    read_json_file,
    write_json_file,
    get_absolute_path_aliases
)

# We build PostgreSQL code in a separate directory (postgres_build) rsynced from the source tree to
# support out-of-source builds. Then, after generating the compilation commands, we rewrite them
# to work with original files (in src/postgres) so that clangd can use them.

# We create multiple directories under $BUILD_ROOT/compile_commands and put a single file named
# compile_commands.json in each directory. This structure is needed due to some quirks of clangd and
# clangd-indexer.

# The directory names listed below vary across two dimensions:
# - YB. vs. PostgreSQL vs. combined.
# - Raw vs. postprocessed. "Postprocessed" means paths are rewritten to refer to the source
#   directory.

COMBINED_POSTPROCESSED_DIR_NAME = 'combined_postprocessed'
PG_POSTPROCESSED_DIR_NAME = 'pg_postprocessed'
YB_POSTPROCESSED_DIR_NAME = 'yb_postprocessed'

COMBINED_RAW_DIR_NAME = 'combined_raw'
YB_RAW_DIR_NAME = 'yb_raw'
PG_RAW_DIR_NAME = 'pg_raw'


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


def get_include_path_arg(include_path: str) -> str:
    return '-I%s' % include_path


class IncludePathRewriter:
    """
    Rewrites include paths in a compilation command line so that they refer to sources in
    src/postgres, not to rsynced sources in build/.../postgres_build.
    """
    original_working_directory: str
    new_working_directory: str
    already_added_include_paths: Set[str]
    original_include_paths: List[str]
    additional_postgres_include_paths: List[str]
    build_root: str
    postgres_install_dir_include_realpath: str
    new_args: List[str]

    def __init__(
            self,
            original_working_directory: str,
            new_working_directory: str,
            build_root: str) -> None:
        self.original_working_directory = original_working_directory
        self.new_working_directory = new_working_directory
        self.already_added_include_paths = set()
        self.original_include_paths = []
        self.additional_postgres_include_paths = []
        self.build_root = build_root
        self.postgres_install_dir_include_realpath = os.path.realpath(
            os.path.join(self.build_root, 'postgres', 'include'))
        self.new_args = []

    def remember_original_include_path(self, original_include_path: str) -> None:
        if (original_include_path not in self.already_added_include_paths and
                original_include_path not in self.original_include_paths):
            self.original_include_paths.append(original_include_path)

    def check_for_postgres_installed_include_path(self, include_path: str) -> None:
        assert os.path.isabs(include_path)
        if (not self.additional_postgres_include_paths and
                os.path.realpath(include_path) == self.postgres_install_dir_include_realpath):
            # If we encounter the include directory build/.../postgres/include, we add these
            # additional include directories at the end of the command line. This will help with
            # finding include files even when the PostgreSQL build is not fully done.
            self.additional_postgres_include_paths.extend([
                os.path.join(YB_SRC_ROOT, 'src', 'postgres', 'src', 'interfaces', 'libpq'),
                os.path.join(
                    self.build_root, 'postgres_build', 'src', 'include'),
                os.path.join(
                    self.build_root, 'postgres_build', 'interfaces', 'libpq')
            ])

    def append_include_path_arg(self, include_path: str) -> None:
        self.new_args.append(get_include_path_arg(include_path))

    def append_new_absolute_include_path(self, new_absolute_include_path: str) -> None:
        assert os.path.isabs(new_absolute_include_path)
        self.append_include_path_arg(new_absolute_include_path)

        # Prevent adding multiple aliases for the same include path.
        self.already_added_include_paths.add(new_absolute_include_path)
        self.already_added_include_paths.add(os.path.abspath(new_absolute_include_path))
        self.already_added_include_paths.add(os.path.realpath(new_absolute_include_path))

    def handle_include_path(self, include_path: str) -> None:
        if os.path.isabs(include_path):
            # This is already an absolute path, append it as is.
            self.check_for_postgres_installed_include_path(include_path)
            self.append_new_absolute_include_path(include_path)
            return

        original_include_path = os.path.realpath(
            os.path.join(self.original_working_directory, include_path))

        # This is a relative path. Rewrite it relative to the new directory where we pretend to
        # be running the compiler (i.e. where Clangd will run the compiler).
        self.remember_original_include_path(original_include_path)
        self.check_for_postgres_installed_include_path(original_include_path)

        new_include_path = os.path.realpath(os.path.join(self.new_working_directory, include_path))
        self.append_new_absolute_include_path(new_include_path)

    def rewrite(self, args: List[str]) -> List[str]:
        for arg in args:
            if not arg.startswith('-I'):
                self.new_args.append(arg)
                continue
            include_path = arg[2:]
            self.handle_include_path(include_path)
        self.new_args.extend(
            [get_include_path_arg(include_path)
             for include_path in
             self.additional_postgres_include_paths + self.original_include_paths])
        return self.new_args


class CompileCommandProcessor:
    build_root: str
    pg_build_root: str
    pg_build_root_aliases: List[str]
    postgres_src_root: str
    extra_args: List[str]

    # For some files, we specify the original directory containing the file (located inside the
    # build directory) as an include directory, because the code might need to include some
    # auto-generated files from there.
    add_original_dir_to_path_for_files: Set[str]

    def __init__(
            self,
            build_root: str,
            extra_args: List[str],
            add_original_dir_to_path_for_files: Set[str]) -> None:
        self.build_root = build_root

        self.pg_build_root = os.path.join(build_root, 'postgres_build')
        self.pg_build_root_aliases = get_absolute_path_aliases(self.pg_build_root)

        self.postgres_src_root = os.path.join(YB_SRC_ROOT, 'src', 'postgres')

        self.extra_args = extra_args

        self.add_original_dir_to_path_for_files = add_original_dir_to_path_for_files

    def postprocess_compile_command(self, compile_command_item: Dict[str, Any]) -> Dict[str, Any]:
        original_working_directory = compile_command_item['directory']
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
        file_path = compile_command_item['file']

        file_path_is_absolute = os.path.isabs(file_path)

        if file_path_is_absolute:
            original_abs_file_path = file_path
        else:
            original_abs_file_path = os.path.join(original_working_directory, file_path)

        new_working_directory = original_working_directory
        for pg_build_root_alias in self.pg_build_root_aliases:
            # Some files only exist in the postgres build directory. We don't switch the work
            # directory of the compiler to the original source directory in those cases.
            if original_working_directory.startswith(pg_build_root_alias + '/'):
                corresponding_src_dir = os.path.join(
                    self.postgres_src_root,
                    os.path.relpath(original_working_directory, pg_build_root_alias))
                if (file_path_is_absolute or
                        os.path.isfile(os.path.join(corresponding_src_dir, file_path))):
                    new_working_directory = corresponding_src_dir
                    break

        if new_working_directory == original_working_directory:
            new_args = arguments
        else:
            new_args = IncludePathRewriter(
                original_working_directory,
                new_working_directory,
                self.build_root,
            ).rewrite(arguments)

        if os.path.basename(file_path) in self.add_original_dir_to_path_for_files:
            new_args.append(f'-I{os.path.dirname(original_abs_file_path)}')

        # Replace the compiler path in compile_commands.json according to user preferences.
        compiler_basename = os.path.basename(new_args[0])
        new_compiler_path = None
        if compiler_basename in ['cc', 'gcc', 'clang']:
            new_compiler_path = os.environ.get('YB_CC_FOR_COMPILE_COMMANDS')
        elif compiler_basename in ['c++', 'g++', 'clang++']:
            new_compiler_path = os.environ.get('YB_CXX_FOR_COMPILE_COMMANDS')
        else:
            logging.warning(
                "Unexpected compiler path: %s. Compile command: %s",
                new_args[0], json.dumps(compile_command_item))
        if new_compiler_path:
            new_args[0] = new_compiler_path

        new_file_path = file_path
        if not os.path.isabs(file_path):
            new_file_path = os.path.join(new_working_directory, file_path)
            if not os.path.isfile(new_file_path):
                new_file_path = file_path

        return {
            'directory': new_working_directory,
            'file': new_file_path,
            'arguments': new_args + self.extra_args
        }


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
        subdir_name: str) -> str:
    return os.path.join(build_root, 'compile_commands', subdir_name, 'compile_commands.json')
