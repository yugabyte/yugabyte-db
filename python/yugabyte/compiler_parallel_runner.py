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

import subprocess
import os
import logging

from typing import List, Any, cast, Type, Tuple, Optional
from dataclasses import dataclass

from overrides import overrides  # type: ignore

from yugabyte.parallel_task_runner import ParallelTaskRunner, ReportHelper
from yugabyte.compile_commands import CompileCommand
from yugabyte.common_util import YB_SRC_ROOT, shlex_join
from yugabyte.file_util import read_file, write_file

from yugabyte.type_util import checked_cast
from yugabyte.compiler_invocation import run_compiler


@dataclass
class CompilerResult:
    cmd: CompileCommand
    compiler_cmd_line: List[str]
    stdout: str
    stderr: str
    exit_code: int
    extra_messages: List[str]
    failure_reason: Optional[str]
    success: bool

    def get_report(self, cmd: CompileCommand) -> str:
        """
        Converts the result to a report string.
        """
        cmd_line_str = shlex_join(self.compiler_cmd_line)

        report_helper = ReportHelper()
        report_helper.add_items([
            ("Compiler input file", cmd.rel_file_path),
            ("Command line", cmd_line_str),
            ("Success", str(self.success)),
        ])

        if self.stdout and self.stdout.strip():
            report_helper.add_item("Compiler standard output", self.stdout.rstrip() + "\n")
        if self.stderr and self.stderr.strip():
            report_helper.add_item("Compiler standard error", self.stderr.rstrip() + "\n")
        if self.exit_code != 0:
            report_helper.add_item("Compiler exit code", str(self.exit_code))
        if self.failure_reason:
            report_helper.add_item("Failure reason", self.failure_reason)
        if self.extra_messages:
            report_helper.add_raw_lines(self.extra_messages)
        return report_helper.as_str()


class CompilerParallelRunner(ParallelTaskRunner):
    compile_commands_path: str
    extra_args: List[str]

    def __init__(
            self,
            parallelism: int,
            compile_commands_path: str,
            extra_args: List[str]) -> None:
        super().__init__(
            parallelism=parallelism,
            task_type=CompileCommand,
            task_result_type=CompilerResult)
        self.compile_commands_path = compile_commands_path
        self.extra_args = extra_args

    @overrides
    def run_task(self, task: Any) -> Any:
        assert isinstance(task, CompileCommand)
        cmd = cast(CompileCommand, task)

        success = True
        compiler_cmd, compiler_result = run_compiler(cmd, error_ok=True, log_command=False)
        file_path = os.path.join(YB_SRC_ROOT, cmd.rel_file_path)
        extra_messages = []
        failure_reason = None
        compiler_stdout = compiler_result.stdout
        compiler_stderr = compiler_result.stderr
        if compiler_result.returncode != 0:
            success = False
            failure_reason = "Could not compile file"

            compiler_output_file_path = file_path + ".compiler_errors.txt"
            report_helper = ReportHelper()
            report_helper.add_items([
                ('Compilation directory', cmd.dir_path),
                ('Compiler command line', compiler_cmd.compiler_args.get_cmd_line_str()),
                ('Compiler standard output', compiler_stdout),
                ('Compiler standard error', compiler_stderr),
                ('Compiler exit code', str(compiler_result.returncode)),
            ])
            report_helper.write_to_file(compiler_output_file_path)

            extra_messages.extend([
                "Could not compile file %s" % cmd.rel_file_path,
                "Saved compiler output details to %s" % compiler_output_file_path,
            ])

        return CompilerResult(
            cmd=cmd,
            compiler_cmd_line=compiler_cmd.compiler_args.immutable_arg_list(),
            stdout=compiler_stdout,
            stderr=compiler_stderr,
            exit_code=compiler_result.returncode,
            extra_messages=extra_messages,
            success=success,
            failure_reason=failure_reason)

    @overrides
    def did_task_succeed(self, task_result: Any) -> bool:
        result = checked_cast(CompilerResult, task_result)
        return result.success

    @overrides
    def report_task_result(self, task: Any, task_result: Any, succeeded: bool) -> None:
        cmd = checked_cast(CompileCommand, task)
        result = checked_cast(CompilerResult, task_result)
        if not succeeded:
            logging.info(result.get_report(cmd))
