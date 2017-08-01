# Copyright (c) YugaByte, Inc.

"""
Helps find the Linuxbrew installation.
"""

import os
from yb.command_util import run_program


linuxbrew_dir = None

YB_SRC_ROOT = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..')


def get_linuxbrew_dir():
    global linuxbrew_dir
    if not linuxbrew_dir:
        find_script_result = run_program(os.path.join(
            YB_SRC_ROOT, 'build-support', 'find_linuxbrew.sh'))
        linuxbrew_dir = find_script_result.stdout.strip()
        if not os.path.isdir(linuxbrew_dir):
            raise RuntimeError(
                    ("Directory returned by the '{}' script does not exist: '{}'. " +
                     "Details: {}").format(
                            find_script_result.program_path,
                            linuxbrew_dir,
                            find_script_result))
    return linuxbrew_dir
