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
from yugabyte.command_util import decode_cmd_output

from yugabyte.type_util import checked_cast
from yugabyte.compiler_invocation import run_compiler


def text_document_to_lines(s: str) -> List[str]:
    lines = [line.rstrip() for line in s.split('\n')]
    while lines and not lines[-1]:
        lines.pop()
    return lines


def contains_consecutive_empty_lines(s: str) -> bool:
    '''
    Returns if the given string contains consecutive empty (or all-whitespace) lines.
    Any empty lines at the end of the string (text file) are ignored.

    >>> contains_consecutive_empty_lines('')
    False
    >>> contains_consecutive_empty_lines('a')
    False
    >>> contains_consecutive_empty_lines('a\\n')
    False
    >>> contains_consecutive_empty_lines('\\n')
    False
    >>> contains_consecutive_empty_lines('a\\n    \\n  \\nb')
    True
    >>> contains_consecutive_empty_lines('\\n    \\n  \\nb')
    True
    >>> contains_consecutive_empty_lines('\\n    \\n')
    False
    '''
    lines = text_document_to_lines(s)
    return any([i for i in range(len(lines) - 1) if not lines[i] and not lines[i + 1]])


def reduce_consecutive_empty_lines_in_str(s: str) -> str:
    '''
    Returns a string with consecutive empty (or all-whitespace) lines reduced to a single empty
    line. All trailing whitespace and empty lines at the end of the file are removed.
    >>> reduce_consecutive_empty_lines_in_str('a\\n    \\n  \\nb')
    'a\\n\\nb'
    >>> reduce_consecutive_empty_lines_in_str('line1   \\nline2 ')
    'line1\\nline2'
    '''
    lines = text_document_to_lines(s)
    new_lines = []
    prev_line = None
    for line in lines:
        if (prev_line is not None and len(prev_line) == 0 and len(line) == 0):
            continue
        new_lines.append(line)
        prev_line = line
    return '\n'.join(new_lines)


def reduce_consecutive_empty_lines_in_file(file_path: str) -> None:
    new_content = reduce_consecutive_empty_lines_in_str(read_file(file_path))
    write_file(content=new_content + '\n', output_file_path=file_path)


@dataclass
class ClangTidyResult:
    cmd: CompileCommand
    clang_tidy_cmd_line: List[str]
    stdout: str
    stderr: str
    compiler_stdout: Optional[str]
    compiler_stderr: Optional[str]
    exit_code: str
    extra_messages: List[str]
    failure_reason: Optional[str]
    success: bool

    def get_report(self, cmd: CompileCommand) -> str:
        """
        Converts the result to a report string.
        """
        cmd_line_str = shlex_join(self.clang_tidy_cmd_line)

        report_helper = ReportHelper()
        report_helper.add_items([
            ("clang-tidy input file", cmd.rel_file_path),
            ("clang-tidy command line", cmd_line_str),
            ("Success", str(self.success)),
            ("clang-tidy standard output", self.stdout.rstrip() + "\n"),
            ("clang-tidy standard error", self.stderr.rstrip() + "\n"),
            (("Compiler standard output", self.compiler_stdout.rstrip() + "\n")
             if self.compiler_stdout else None),
            (("Compiler standard error", self.compiler_stderr.rstrip() + "\n")
             if self.compiler_stderr else None),
            ("clang-tidy exit code", str(self.exit_code)),
            (("Failure reason", self.failure_reason)
             if self.failure_reason else None),
        ])
        if self.extra_messages:
            report_helper.add_raw_lines(self.extra_messages)
        return report_helper.as_str()


class ClangTidyRunner(ParallelTaskRunner):
    clang_tidy_path: str
    compile_commands_path: str
    extra_args: List[str]
    apply_fixes: bool

    def __init__(
            self,
            parallelism: int,
            clang_tidy_path: str,
            compile_commands_path: str,
            extra_args: List[str],
            apply_fixes: bool) -> None:
        super().__init__(
            parallelism=parallelism,
            task_type=CompileCommand,
            task_result_type=ClangTidyResult)
        self.clang_tidy_path = clang_tidy_path
        self.compile_commands_path = compile_commands_path
        self.extra_args = extra_args
        self.apply_fixes = apply_fixes

    @overrides
    def run_task(self, task: Any) -> Any:
        assert isinstance(task, CompileCommand)
        cmd = cast(CompileCommand, task)

        success = True

        failure_reason = None
        clang_tidy_args = [
            self.clang_tidy_path,
            '-p',
            os.path.dirname(self.compile_commands_path),
            cmd.rel_file_path,
        ]
        compiler_stdout = None
        compiler_stderr = None
        if self.apply_fixes:
            clang_tidy_args.append('--fix-notes')
            file_path = os.path.join(YB_SRC_ROOT, cmd.rel_file_path)
            file_contents_backup = read_file(file_path)
            had_consecutive_empty_lines = contains_consecutive_empty_lines(file_contents_backup)

        clang_tidy_args.extend(self.extra_args)

        process = subprocess.Popen(
            clang_tidy_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        decoded_stdout = decode_cmd_output(stdout)
        decoded_stderr = decode_cmd_output(stderr)
        exit_code = process.returncode
        extra_messages = []
        if not isinstance(exit_code, int):
            extra_messages.append("Invaild exit code: %s" % exit_code)
            exit_code = 1

        if self.apply_fixes:
            if not had_consecutive_empty_lines:
                # If the file did not have consecutive empty lines before running clang-tidy,
                # any new such lines must have been added by clang-tidy, so we reduce them.
                reduce_consecutive_empty_lines_in_file(file_path)

            compiler_cmd, compiler_result = run_compiler(cmd, error_ok=True, log_command=False)
            if compiler_result.returncode != 0:
                compiler_stdout = compiler_result.stdout
                compiler_stderr = compiler_result.stderr
                success = False
                failure_reason = "Could not compile fixed file"

                file_path_without_ext, file_ext = os.path.splitext(file_path)
                file_that_cannot_be_compiled = (
                    file_path_without_ext + ".bad_clang_tidy_output" + file_ext)
                compiler_output_file_path = file_path + ".compiler_errors.txt"
                write_file(content=read_file(file_path),
                           output_file_path=file_that_cannot_be_compiled)
                report_helper = ReportHelper()
                report_helper.add_items([
                    ("Compilation directory", cmd.dir_path),
                    ("Compiler command line", compiler_cmd.compiler_args.get_cmd_line_str()),
                    ("Compiler standard output", compiler_stdout.rstrip() + "\n"),
                    ("Compiler standard error", compiler_stderr.rstrip() + "\n"),
                    ("Compiler exit code", str(compiler_result.returncode)),
                    ("Source file that could not be compiled copied to",
                     file_that_cannot_be_compiled),
                ])
                report_helper.write_to_file(compiler_output_file_path)

                extra_messages.extend([
                    "Could not compile fixed file %s" % cmd.rel_file_path,
                    "Saved clang-tidy output that could not be compiled to %s" %
                    file_that_cannot_be_compiled,
                    "Saved compiler output details to %s" % compiler_output_file_path,
                ])
                # Revert changes made by clang-tidy to the file.
                write_file(content=file_contents_backup, output_file_path=file_path)

        if exit_code != 0:
            success = False
            if failure_reason is None:
                failure_reason = "clang-tidy failed"

        return ClangTidyResult(
            cmd=cmd,
            clang_tidy_cmd_line=clang_tidy_args,
            stdout=decoded_stdout,
            stderr=decoded_stderr,
            exit_code=exit_code,
            extra_messages=extra_messages,
            success=success,
            failure_reason=failure_reason,
            compiler_stdout=compiler_stdout,
            compiler_stderr=compiler_stderr)

    @overrides
    def did_task_succeed(self, task_result: Any) -> bool:
        result = checked_cast(ClangTidyResult, task_result)
        return result.success

    @overrides
    def report_task_result(self, task: Any, task_result: Any, succeeded: bool) -> None:
        cmd = checked_cast(CompileCommand, task)
        result = checked_cast(ClangTidyResult, task_result)
        logging.info(result.get_report(cmd))
