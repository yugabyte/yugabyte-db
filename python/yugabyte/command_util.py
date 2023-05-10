#
# Copyright (c) YugaByte, Inc.
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
#
"""
Copyright (c) YugaByte, Inc.

This module provides utilities for running commands.
"""

import os
import platform
import shutil
import subprocess
import logging

from typing import Union, List, Optional, IO

from yugabyte.common_util import shlex_join
from yugabyte.file_util import mkdir_p


class ProgramResult:
    cmd_line: List[str]
    returncode: int
    stdout: str
    stderr: str

    # Error message that would have been raised if error_ok was False.
    error_msg: Optional[str]

    program_path: str

    def __init__(
            self,
            cmd_line: List[str],
            returncode: int,
            stdout: str,
            stderr: str,
            error_msg: Optional[str],
            program_path: str) -> None:
        self.cmd_line = cmd_line
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.error_msg = error_msg
        self.program_path = program_path

    # Can't use autorepr here because this file is used indirectly from run_tests_on_spark.py, where
    # the dependencies from requirements.txt are unavailable.
    def __str__(self) -> str:
        return ('ProgramResult('
                'cmd_line=%s, '
                'returncode=%d, '
                'stdout=%s, '
                'stderr=%s, '
                'error_msg=%s, '
                'program_path=%s') % (
            self.cmd_line, self.returncode, self.stdout, self.stderr, self.error_msg,
            self.program_path
        )


def trim_output(output: Union[str, bytes], max_lines: int) -> str:
    if isinstance(output, bytes):
        output = output.decode('utf-8')
    lines = output.split("\n")
    if len(lines) <= max_lines:
        return output
    return "\n".join(lines[:max_lines] + ['({} lines skipped)'.format(len(lines) - max_lines)])


def run_program(
        args: Union[str, List[str]],
        error_ok: bool = False,
        max_error_lines: int = 100,
        cwd: Optional[str] = None,
        log_command: bool = False) -> ProgramResult:
    """
    Run the given program identified by its argument list, and return a ProgramResult object.

    @param error_ok False to raise an exception on errors.
    """
    if not isinstance(args, list):
        args = [args]
    if log_command:
        if cwd is None:
            dir_for_logging = os.getcwd()
        else:
            dir_for_logging = cwd
        logging.info("Running command: %s (in directory: %s)", shlex_join(args), dir_for_logging)
    try:
        program_subprocess = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd)
    except OSError:
        logging.error("Failed to run program {}".format(args))
        raise

    program_stdout, program_stderr = program_subprocess.communicate()
    error_msg = None
    if program_subprocess.returncode != 0:
        error_msg = "Non-zero exit code {} from: {} ; stdout: '{}' stderr: '{}'".format(
                program_subprocess.returncode, shlex_join(args),
                trim_output(program_stdout.strip(), max_error_lines),
                trim_output(program_stderr.strip(), max_error_lines))
        if not error_ok:
            raise RuntimeError(error_msg)
    return ProgramResult(
        cmd_line=args,
        returncode=program_subprocess.returncode,
        stdout=program_stdout.strip().decode('utf-8'),
        stderr=program_stderr.strip().decode('utf-8'),
        error_msg=error_msg,
        program_path=os.path.realpath(args[0])
    )


def copy_deep(src: str, dst: str, create_dst_dir: bool = False) -> None:
    """
    Does recursive copy of src path to dst path. Copies symlinks as symlinks. Doesn't overwrite
    existing files and symlinks (even if they are broken) in Linux.
    In Darwin, it overwrite the files if the source mtime is newer than the destination mtime.
    """
    system_is_darwin = platform.system().lower() == "darwin"
    if create_dst_dir:
        mkdir_p(os.path.dirname(dst))
    src_is_link = os.path.islink(src)
    dst_exists = os.path.lexists(dst)
    if os.path.isdir(src) and not src_is_link:
        logging.debug("Copying directory {} to {}".format(src, dst))
        mkdir_p(dst)
        for name in os.listdir(src):
            copy_deep(os.path.join(src, name), os.path.join(dst, name))
    elif src_is_link:
        if dst_exists:
            return
        target = os.readlink(src)
        logging.debug("Creating symlink {} -> {}".format(dst, target))
        os.symlink(target, dst)
    else:
        if dst_exists:
            if not system_is_darwin:
                return
            # Only overwrite the file if the source is newer than the destination.
            if os.path.getmtime(src) <= os.path.getmtime(dst):
                return
        logging.debug("Copying file {} to {}".format(src, dst))
        # Preserve the file attributes.
        shutil.copy2(src, dst)


def decode_cmd_output(output: Optional[Union[str, bytes, IO[bytes]]]) -> str:
    """
    Decode the output of a command obtained from subprocess.Popen to a string.
    """
    if output is None:
        return ''
    if isinstance(output, str):
        return output
    if isinstance(output, bytes):
        return output.decode('utf-8')
    # This is a file-like object, but we can't check for that using isinstance.
    return output.read().decode('utf-8')
