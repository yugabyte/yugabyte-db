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

from typing import Set, Dict, List, Any, Optional, Hashable

from yugabyte.postgres_build_util import POSTGRES_BUILD_SUBDIR
from yugabyte.common_util import YB_SRC_ROOT, get_absolute_path_aliases, join_paths_if_needed
from yugabyte.compiler_args import get_preprocessor_definition_values
from yugabyte.include_path_rewriter import IncludePathRewriter
from yugabyte.compile_commands import get_arguments_from_compile_command_item, CompileCommand
from yugabyte.file_util import assert_absolute_path


class CompileCommandProcessor:
    build_root: str
    pg_build_root: str
    pg_build_root_aliases: List[str]
    postgres_src_root: str

    # For some files, we specify the original directory containing the file (located inside the
    # build directory) as an include directory, because the code might need to include some
    # auto-generated files from there.
    add_original_dir_to_path_for_files: Set[str]

    compiler_definitions: Dict[str, str]

    resolved_c_compiler: str
    resolved_cxx_compiler: str

    def __init__(
            self,
            build_root: str,
            add_original_dir_to_path_for_files: Set[str],
            resolved_c_compiler: str,
            resolved_cxx_compiler: str) -> None:
        """
        :param build_root: The build root.
        :param add_original_dir_to_path_for_files: A set of file paths relative to the build root
            for which we should add the original directory containing the file to the include path.
        :param resolved_c_compiler: The path to the actual C compiler (not a compiler wrapper) to
            use for compilation commands.
        :param resolved_cxx_compiler: Same for the C++ compiler.
        """
        self.build_root = build_root

        self.pg_build_root = os.path.join(build_root, POSTGRES_BUILD_SUBDIR)
        self.pg_build_root_aliases = get_absolute_path_aliases(self.pg_build_root)

        self.postgres_src_root = os.path.join(YB_SRC_ROOT, 'src', 'postgres')

        self.add_original_dir_to_path_for_files = add_original_dir_to_path_for_files

        self.compiler_definitions = {}

        self.resolved_c_compiler = resolved_c_compiler
        self.resolved_cxx_compiler = resolved_cxx_compiler

    def infer_preprocessor_definition(self, def_name: str, commands: List[Dict[str, Any]]) -> None:
        values: Set[str] = set()
        for command in commands:
            arguments = get_arguments_from_compile_command_item(command)
            def_value_map = get_preprocessor_definition_values(arguments)
            if def_name in def_value_map:
                values |= set(def_value_map[def_name])

        if not values:
            logging.debug("Did not find a value for preprocessor definition %s in compiler "
                          "commands, skipping", def_name)
            return
        if len(values) != 1:
            raise ValueError(
                "Expected exactly one value for compiler definition %s, got %s" %
                (def_name, sorted(values)))
        value = list(values)[0]
        logging.debug(
            "Inferred preprocessor definition %s=%s, will append it to all compiler commands",
            def_name, value)
        self.compiler_definitions[def_name] = value

    def apply_preprocessor_definitions(self, args: List[str]) -> None:
        def_value_map = get_preprocessor_definition_values(args)
        for def_name, def_value in self.compiler_definitions.items():
            if def_name not in def_value_map:
                args.append('-D%s=%s' % (def_name, def_value))

    def resolve_compiler_path(self, compiler_basename: str) -> Optional[str]:
        if compiler_basename in ['cc', 'gcc', 'clang']:
            return os.environ.get('YB_CC_FOR_COMPILE_COMMANDS') or self.resolved_c_compiler
        if compiler_basename in ['c++', 'g++', 'clang++']:
            return os.environ.get('YB_CXX_FOR_COMPILE_COMMANDS') or self.resolved_cxx_compiler
        return None

    def postprocess_compile_command(self, compile_command_item: Dict[str, Any]) -> Dict[str, Any]:
        original_working_directory = compile_command_item['directory']
        assert_absolute_path(original_working_directory)
        original_working_directory = os.path.realpath(original_working_directory)

        arguments = get_arguments_from_compile_command_item(compile_command_item)

        file_path = compile_command_item['file']

        file_path_is_absolute = os.path.isabs(file_path)

        if file_path_is_absolute:
            original_abs_file_path = file_path
        else:
            original_abs_file_path = os.path.join(original_working_directory, file_path)

        # It is important not to use realpath here, because the PostgreSQL source tree contains many
        # symlinks and we want to preserve separate compilation database entries for these
        # different copies of the same file. Each of them can have slightly different compiler
        # arguments.
        #
        # Examples of symlinks in the PostgreSQL source tree:
        # https://gist.githubusercontent.com/mbautin/ca57f163992c530ded37b40d48df1aaa/raw
        original_abs_file_path = os.path.abspath(original_abs_file_path)

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

        new_abs_file_path = original_abs_file_path
        if not os.path.isabs(file_path):
            new_file_path_candidate = os.path.join(new_working_directory, file_path)
            if os.path.isfile(new_file_path_candidate):
                new_abs_file_path = new_file_path_candidate
            else:
                new_abs_file_path = os.path.join(original_working_directory, file_path)
            new_abs_file_path = os.path.abspath(new_abs_file_path)

        new_args = IncludePathRewriter(
            original_working_directory,
            new_working_directory,
            self.build_root,
        ).rewrite(arguments)

        # Replace input and output file paths in the compilation command with absolute paths
        # so they don't depend on the working directory.
        for i in range(len(new_args) - 1):
            if new_args[i] == '-o':
                output_path = new_args[i + 1]
                if not os.path.isabs(output_path):
                    new_args[i + 1] = os.path.abspath(
                        os.path.join(original_working_directory, output_path))
        for i in range(len(new_args)):
            if (not os.path.isabs(new_args[i]) and
                    os.path.realpath(os.path.join(original_working_directory, new_args[i])) ==
                    os.path.realpath(original_abs_file_path)):
                # This is the input file argument, given as a relative path. Replace it with the
                # absolute path so we have more flexibility in changing the working directory.
                new_args[i] = new_abs_file_path

        new_rel_file_path = os.path.relpath(new_abs_file_path, YB_SRC_ROOT)
        new_rel_file_dir = os.path.dirname(new_rel_file_path)
        new_rel_working_dir = os.path.relpath(new_working_directory, YB_SRC_ROOT)

        if (os.path.basename(file_path) in ['pgtz.c', 'strftime.c', 'localtime.c'] and
                new_rel_file_dir == 'src/postgres/src/timezone' and
                new_rel_working_dir == 'src/postgres/src/backend'):
            # Reduce duplicate compilation commands for a few time- and timezone-related files
            # by changing the working directory. These files are probably compiled multiple times
            # in different working directories, but their compilation commands are mostly identical
            # so we only include them once to avoid confusion.
            new_working_directory = os.path.dirname(new_abs_file_path)

        if os.path.basename(file_path) in self.add_original_dir_to_path_for_files:
            new_args.append(f'-I{os.path.dirname(original_abs_file_path)}')

        # Replace the compiler path in compile_commands.json according to user preferences.
        compiler_basename = os.path.basename(new_args[0])
        new_compiler_path = self.resolve_compiler_path(compiler_basename)
        if new_compiler_path:
            new_args[0] = new_compiler_path
        else:
            logging.warning(
                "Unexpected compiler path: %s. Compile command: %s",
                new_args[0], json.dumps(compile_command_item))

        self.apply_preprocessor_definitions(new_args)

        return {
            'directory': new_working_directory,
            'file': new_abs_file_path,
            'arguments': new_args
        }

    def deduplicate_commands(self, commands: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """
        Removes duplicate commands from the list. The commands are considered duplicates if they
        have the same directory and the same arguments.
        """
        parsed_commands: List[CompileCommand] = [
            CompileCommand.from_json_obj(command_item) for command_item in commands
        ]
        cmds_without_exact_duplicates: List[CompileCommand] = sorted(set(parsed_commands))
        if len(cmds_without_exact_duplicates) != len(parsed_commands):
            logging.info(
                "Removed %d exact duplicate compile commands",
                len(parsed_commands) - len(cmds_without_exact_duplicates))

        seen_equivalence_classes: Set[Hashable] = set()
        filtered_cmds: List[CompileCommand] = []

        num_non_exact_deduped = 0
        for cmd in cmds_without_exact_duplicates:
            equivalence_key = cmd.equivalence_key()
            if equivalence_key not in seen_equivalence_classes:
                seen_equivalence_classes.add(equivalence_key)
                filtered_cmds.append(cmd)
            else:
                num_non_exact_deduped += 1
        if num_non_exact_deduped:
            logging.info("Removed %d non-exact duplicate compile commands", num_non_exact_deduped)

        return [cmd.as_json_obj() for cmd in filtered_cmds]
