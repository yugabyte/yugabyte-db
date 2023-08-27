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

from yugabyte.compile_commands import CompileCommand
from yugabyte.command_util import ProgramResult, run_program
from yugabyte.file_util import delete_file_if_exists

import copy

from typing import Tuple


def run_compiler(
        cmd: CompileCommand,
        error_ok: bool,
        log_command: bool = True) -> Tuple[CompileCommand, ProgramResult]:
    """
    Run the compiler on the given command, and return the exact command line used to do so.
    We don't plan to use the compiler output, only to check that it compiles, so we modify the
    output file name and delete the compilation output file immediately.
    """
    updated_cmd = copy.deepcopy(cmd)

    working_dir_path = updated_cmd.dir_path

    updated_cmd.compiler_args.append_to_output_path('.tmp')
    updated_cmd.compiler_args.remove_dependency_related_flags()
    output_path = updated_cmd.compiler_args.get_output_path()

    try:
        preprocessor_result = run_program(
            args=['ccache'] + list(updated_cmd.compiler_args.args),
            cwd=working_dir_path,
            log_command=log_command,
            error_ok=error_ok,
        )
    finally:
        delete_file_if_exists(output_path)

    return updated_cmd, preprocessor_result
