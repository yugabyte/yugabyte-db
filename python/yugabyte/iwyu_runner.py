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

"""
Runs Include What You Use (IWYU) on YugabyteDB C++ source files.

This module provides a parallel runner for IWYU that uses the compilation database
(compile_commands.json) to analyze C++ source files.
"""

import subprocess
import os
import logging
import re

from typing import List, Any, Optional
from dataclasses import dataclass, field

from overrides import overrides  # type: ignore

from yugabyte.parallel_task_runner import ParallelTaskRunner, ReportHelper
from yugabyte.compile_commands import CompileCommand
from yugabyte.common_util import YB_SRC_ROOT, shlex_join
from yugabyte.command_util import decode_cmd_output
from yugabyte.type_util import checked_cast


@dataclass
class IWYUResult:
    """Result of running IWYU on a single file."""
    cmd: CompileCommand
    iwyu_cmd_line: List[str]
    stdout: str
    stderr: str
    exit_code: int
    has_suggestions: bool
    success: bool
    failure_reason: Optional[str] = None
    extra_messages: List[str] = field(default_factory=list)

    def get_report(self, cmd: CompileCommand) -> str:
        """Converts the result to a report string."""
        cmd_line_str = shlex_join(self.iwyu_cmd_line)

        report_helper = ReportHelper()
        report_helper.add_items([
            ("IWYU input file", cmd.rel_file_path),
            ("IWYU command line", cmd_line_str),
            ("Success", str(self.success)),
            ("Has suggestions", str(self.has_suggestions)),
        ])

        # Only show output if there are suggestions or errors
        if self.has_suggestions or not self.success:
            report_helper.add_items([
                ("IWYU standard output", self.stdout.rstrip() + "\n"),
                ("IWYU standard error", self.stderr.rstrip() + "\n"),
                ("IWYU exit code", str(self.exit_code)),
            ])
            if self.failure_reason:
                report_helper.add_item("Failure reason", self.failure_reason)

        if self.extra_messages:
            report_helper.add_raw_lines(self.extra_messages)

        return report_helper.as_str()


def parse_iwyu_output(stderr: str) -> bool:
    """
    Parses IWYU output to determine if there are any include suggestions.

    IWYU outputs its suggestions to stderr. If there are suggestions to add or remove
    includes, IWYU will output lines like:
    - "should add these lines:"
    - "should remove these lines:"

    Returns True if there are suggestions, False otherwise.
    """
    # Check for common IWYU suggestion patterns
    suggestion_patterns = [
        r'should add these lines:',
        r'should remove these lines:',
        r'The full include-list for',
    ]

    for pattern in suggestion_patterns:
        if re.search(pattern, stderr):
            return True

    return False


class IWYURunner(ParallelTaskRunner):
    """Parallel runner for Include What You Use (IWYU)."""

    iwyu_path: str
    compile_commands_path: str
    extra_args: List[str]
    mapping_file: Optional[str]
    verbose: bool

    def __init__(
            self,
            parallelism: int,
            iwyu_path: str,
            compile_commands_path: str,
            extra_args: Optional[List[str]] = None,
            mapping_file: Optional[str] = None,
            verbose: bool = False) -> None:
        super().__init__(
            parallelism=parallelism,
            task_type=CompileCommand,
            task_result_type=IWYUResult)
        self.iwyu_path = iwyu_path
        self.compile_commands_path = compile_commands_path
        self.extra_args = extra_args or []
        self.mapping_file = mapping_file
        self.verbose = verbose

    def _build_iwyu_args(self, cmd: CompileCommand) -> List[str]:
        """Build the command line arguments for IWYU."""
        iwyu_args = [self.iwyu_path]

        # Add mapping file if specified
        if self.mapping_file and os.path.exists(self.mapping_file):
            iwyu_args.extend(['-Xiwyu', f'--mapping_file={self.mapping_file}'])

        # Add common IWYU options
        # --no_fwd_decls: Don't suggest forward declarations (can be noisy)
        # --max_line_length: Set max line length for output
        iwyu_args.extend(['-Xiwyu', '--max_line_length=100'])

        # Add any extra args
        for arg in self.extra_args:
            if arg.startswith('-Xiwyu'):
                iwyu_args.append(arg)
            else:
                iwyu_args.extend(['-Xiwyu', arg])

        # Add the compiler arguments from the compilation command
        # Skip the first argument (compiler path) and the output file arguments
        compiler_args = cmd.compiler_args.args[1:]

        # Filter out arguments that IWYU doesn't understand
        filtered_args = []
        skip_next = False
        for i, arg in enumerate(compiler_args):
            if skip_next:
                skip_next = False
                continue

            # Skip output file arguments
            if arg == '-o':
                skip_next = True
                continue
            if arg.startswith('-o'):
                continue

            # Skip some compiler-specific flags that IWYU might not understand
            if arg in ['-c', '-MD', '-MF', '-MT']:
                if arg in ['-MF', '-MT']:
                    skip_next = True
                continue

            # Skip dependency file arguments
            if arg.startswith('-MF') or arg.startswith('-MT'):
                continue

            filtered_args.append(arg)

        iwyu_args.extend(filtered_args)

        return iwyu_args

    @overrides
    def run_task(self, task: Any) -> Any:
        assert isinstance(task, CompileCommand)
        cmd = checked_cast(CompileCommand, task)

        iwyu_args = self._build_iwyu_args(cmd)

        success = True
        failure_reason = None
        extra_messages: List[str] = []

        try:
            process = subprocess.Popen(
                iwyu_args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=cmd.dir_path)
            stdout_bytes, stderr_bytes = process.communicate(timeout=300)  # 5 minute timeout
            stdout = decode_cmd_output(stdout_bytes)
            stderr = decode_cmd_output(stderr_bytes)
            exit_code = process.returncode
        except subprocess.TimeoutExpired as e:
            process.kill()
            stdout = ""
            stderr = f"IWYU timed out after 300 seconds for {cmd.rel_file_path}"
            exit_code = -1
            success = False
            failure_reason = "Timeout"
        except Exception as e:
            stdout = ""
            stderr = str(e)
            exit_code = -1
            success = False
            failure_reason = f"Exception: {type(e).__name__}"

        # IWYU returns non-zero exit code when it has suggestions
        # Exit code 0 means no changes needed
        # Exit code 1 means suggestions are available
        # Exit code 2+ typically means an error occurred
        has_suggestions = parse_iwyu_output(stderr)

        if exit_code > 1:
            success = False
            if failure_reason is None:
                failure_reason = f"IWYU exited with code {exit_code}"

        return IWYUResult(
            cmd=cmd,
            iwyu_cmd_line=iwyu_args,
            stdout=stdout,
            stderr=stderr,
            exit_code=exit_code,
            has_suggestions=has_suggestions,
            success=success,
            failure_reason=failure_reason,
            extra_messages=extra_messages)

    @overrides
    def did_task_succeed(self, task_result: Any) -> bool:
        result = checked_cast(IWYUResult, task_result)
        return result.success

    @overrides
    def report_task_result(self, task: Any, task_result: Any, succeeded: bool) -> None:
        cmd = checked_cast(CompileCommand, task)
        result = checked_cast(IWYUResult, task_result)

        # Only log detailed output for files with suggestions or failures
        if result.has_suggestions or not succeeded:
            logging.info(result.get_report(cmd))
        elif self.verbose:
            logging.info(f"IWYU: {cmd.rel_file_path} - OK (no suggestions)")
