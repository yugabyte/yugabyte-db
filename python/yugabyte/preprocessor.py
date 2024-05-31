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

import copy

from yugabyte.compile_commands import CompileCommand
from yugabyte.command_util import run_program, ProgramResult

from typing import List, Tuple


# Flags appended to the command line when running the preprocessor only.
PREPROCESSOR_ONLY_FLAGS = ['-E', '-dI']


def run_preprocessor(cmd: CompileCommand, error_ok: bool) -> Tuple[CompileCommand, ProgramResult]:
    """
    Run the preprocessor on the given command, and return the exact command line used to do so.
    The returned command line is somewhat modified from the original command line.
    The caller can obtain the path of the preprocessed file from the returned command line.
    If error_ok is True, in case of preprocessor error, returns a ProgramResult object, otherwise
    raises an exception.
    """
    updated_cmd = copy.deepcopy(cmd)

    working_dir_path = updated_cmd.dir_path

    updated_cmd.compiler_args.extend(PREPROCESSOR_ONLY_FLAGS)

    updated_cmd.compiler_args.append_to_output_path('.pp')
    updated_cmd.compiler_args.remove_dependency_related_flags()

    preprocessor_result = run_program(
        args=updated_cmd.compiler_args.immutable_arg_list(),
        cwd=working_dir_path,
        log_command=True,
        error_ok=error_ok
    )
    return updated_cmd, preprocessor_result
