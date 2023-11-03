#!/usr/bin/env python3

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

import argparse
import logging
import os
import shlex
import subprocess
import sys

from typing import List, Optional, Set, Dict, Union, NoReturn

# TODO: do not modify system path like this, create a wrapper script instead.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'python'))  # noqa


from yugabyte import remote
from yugabyte.common_util import init_logging, YB_SRC_ROOT


def add_extra_yb_build_args(yb_build_args: List[str], extra_args: List[str]) -> List[str]:
    """
    Inserts extra arguments into a list of yb_build.sh arguments. If a "--" argument is present,
    new arguments are inserted before it, because the rest of yb_build.sh's arguments may be passed
    along to yet another command.

    :param yb_build_args: existing yb_build.sh arguments
    :param extra_args: extra arguments to insert
    :return: new list of yb_build.sh arguments
    """
    for i in range(len(yb_build_args)):
        if yb_build_args[i] == '--':
            return yb_build_args[:i] + extra_args + yb_build_args[i:]

    return yb_build_args + extra_args


class ArgumentParserError(Exception):
    def report_error_and_exit(self, extra_msg: str = "") -> NoReturn:
        msg = "Error parsing arguments to remote_build.py: %s\n%s" % (self, extra_msg)
        if not msg.endswith('\n'):
            msg += '\n'
        sys.stderr.write(msg)
        sys.exit(1)


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> NoReturn:
        raise ArgumentParserError(message)


def main() -> None:
    parser = ThrowingArgumentParser(prog=sys.argv[0])

    arg_list = sys.argv[1:]
    remote_build_args_file = 'YB_REMOTE_BUILD_ARGS_FILE'
    if remote_build_args_file in os.environ:
        with open(os.environ[remote_build_args_file], "wt") as out:
            for arg in arg_list:
                out.write(" " + shlex.quote(arg))

    parser.add_argument('build_args', nargs=argparse.REMAINDER,
                        help='arguments for yb_build.sh')

    remote.add_common_args(parser)
    parser.add_argument('--patch-path', action='store_true',
                        help='Patch file path to get CLion friendly navigation.')
    remote.handle_yb_build_cmd_line()
    try:
        args = parser.parse_args()
    except ArgumentParserError as ex:
        yb_build_args_validation_result = subprocess.run(
            [os.path.join(YB_SRC_ROOT, 'yb_build.sh'), '--validate-args-only'] + arg_list)
        if yb_build_args_validation_result.returncode != 0:
            ex.report_error_and_exit(
                "Also could not interpret them as arguments to yb_build.py (see the error above). "
                "If you want to combine arguments to both scripts, specify remote_build.py "
                "arguments first, then --, then arguments to yb_build.sh.")

        arg_list = ['--'] + arg_list
        try:
            args = parser.parse_args(args=arg_list)
        except ArgumentParserError as ex:
            ex.report_error_and_exit()

    init_logging(verbose=args.verbose)

    remote.load_profile(args, args.profile)
    remote.apply_default_arg_values(args)

    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    remote.log_args(args)

    escaped_remote_path = remote.sync_changes_with_args(args)

    if args.skip_build:
        sys.exit(0)

    remote_args = []
    if args.build_type:
        remote_args.append(args.build_type)

    if len(args.build_args) != 0 and args.build_args[0] == '--':
        remote_args += args.build_args[1:]
    else:
        remote_args += args.build_args

    if '--host-for-tests' not in remote_args and 'YB_HOST_FOR_RUNNING_TESTS' in os.environ:
        remote_args = add_extra_yb_build_args(
            remote_args,
            ['--host-for-tests', os.environ['YB_HOST_FOR_RUNNING_TESTS']])

    remote.exec_command(
        host=args.host,
        escaped_remote_path=escaped_remote_path,
        script_name='yb_build.sh',
        script_args=remote_args,
        should_quote_args=True,
        extra_ssh_args=remote.process_extra_ssh_args(args.extra_ssh_args),
        patch_path=args.patch_path)


if __name__ == '__main__':
    main()
