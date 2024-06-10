#!/usr/bin/env python3

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

"""
This script is used as a wrapper for the /bin/install tool in the Postgres build.
It ensures we are not copying header files unnecessarily and causing recompilation of a large number
of source files as a result.
"""

import sys
import subprocess
import os

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # nopep8

from yugabyte.common_util import shlex_join, are_files_equal


def main() -> None:
    args = sys.argv[1:]

    cmd_line_str = shlex_join(args)

    assert os.path.basename(args[0]) in ['install', 'ginstall'], \
        "Expected the first argument to be the path of the 'install' or 'ginstall' UNIX tool, " \
        "found: " + args[0]

    skip_next_arg = False
    other_args = []
    for arg in args[1:]:
        if arg == '-m':
            skip_next_arg = True
            continue
        if arg.startswith('-') or skip_next_arg:
            skip_next_arg = False
            continue
        other_args.append(arg)

    if len(other_args) < 2:
        raise ValueError(
            "Could not parse the install tool command line: %s. "
            "Expected 2 or more positional arguments, got: %s" % (
                cmd_line_str, other_args))

    src_file_paths = other_args[:-1]
    dst_file_or_dir_path = other_args[-1]

    if len(src_file_paths) > 1 and os.path.isfile(dst_file_or_dir_path):
        raise ValueError(
            "Error parsing install command line: %s. Multiple source paths given: %s, "
            "but destination is an existing file: %s" % (
                cmd_line_str, src_file_paths, dst_file_or_dir_path
            ))

    should_install = False

    dst_is_dir = (
        dst_file_or_dir_path.endswith('/') or
        os.path.isdir(dst_file_or_dir_path) or
        len(src_file_paths) > 1
    )
    debug_mode = os.getenv('YB_DEBUG_INSTALL_WRAPPER') == '1'

    for src_file_path in src_file_paths:
        if not os.path.exists(src_file_path):
            raise IOError("File does not exist: %s. Command line: %s" % (
                src_file_path, cmd_line_str))
        if dst_is_dir:
            dst_file_path = os.path.join(dst_file_or_dir_path, os.path.basename(src_file_path))
        else:
            dst_file_path = dst_file_or_dir_path

        if os.path.exists(src_file_path) and os.path.exists(dst_file_path):
            if not are_files_equal(src_file_path, dst_file_path):
                should_install = True
                if debug_mode:
                    print('DEBUG: Files are different: %s and %s, should install' % (
                        src_file_path, dst_file_path))
        else:
            if debug_mode:
                print('DEBUG: Should install %s to %s, src_file_exists=%s, dst_file_exists=%s' % (
                      src_file_path,
                      dst_file_path,
                      os.path.exists(src_file_path),
                      os.path.exists(dst_file_path)))
            should_install = True

    if should_install:
        print("Executing install command: %s" % cmd_line_str)
        subprocess.check_call(args)
    else:
        print("Skipping install command (all files are up to date): %s" % cmd_line_str)


if __name__ == '__main__':
    main()
