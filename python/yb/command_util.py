"""
Copyright (c) YugaByte, Inc.

This module provides utilities for running commands.
"""

import os
import subprocess
import logging

from collections import namedtuple


ProgramResult = namedtuple('ProgramResult',
                           ['cmd_line',
                            'returncode',
                            'stdout',
                            'stderr',
                            'error_msg',
                            'program_path'])


def run_program(args, error_ok=False):
    """
    Run the given program identified by its argument list, and return a ProgramResult object.

    @param error_ok True to raise an exception on errors.
    """
    if not isinstance(args, list):
        args = [args]
    try:
        program_subprocess = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
    except OSError:
        logging.error("Failed to run program {}".format(args))
        raise

    program_stdout, program_stderr = program_subprocess.communicate()
    error_msg = None
    if program_subprocess.returncode != 0:
        error_msg = "Non-zero exit code {} from: {}, stdout: '{}', stderr: '{}'".format(
                program_subprocess.returncode, args,
                program_stdout.strip(), program_stderr.strip())
        if not error_ok:
            raise RuntimeError(error_msg)
    return ProgramResult(cmd_line=args,
                         program_path=os.path.realpath(args[0]),
                         returncode=program_subprocess.returncode,
                         stdout=program_stdout.strip(),
                         stderr=program_stderr.strip(),
                         error_msg=error_msg)
