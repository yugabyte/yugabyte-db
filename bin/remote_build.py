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

import sys
import os
import argparse
import logging

from typing import List, Optional, Set, Dict, Union

# TODO: do not modify system path like this, create a wrapper script instead.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'python'))  # noqa


from yb import remote
from yb.common_util import init_env


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


def main() -> None:
    parser = argparse.ArgumentParser(prog=sys.argv[0])

    parser.add_argument('build_args', nargs=argparse.REMAINDER,
                        help='arguments for yb_build.sh')

    remote.add_common_args(parser)
    remote.handle_yb_build_cmd_line()
    args = parser.parse_args()
    init_env(verbose=args.verbose)

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
        extra_ssh_args=remote.process_extra_ssh_args(args.extra_ssh_args))


if __name__ == '__main__':
    main()
