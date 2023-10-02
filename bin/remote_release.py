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
import os
import sys
import shlex

# TODO: do not modify system path like this, create a wrapper script instead.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'python'))  # noqa

from yugabyte import remote


def main() -> None:
    parser = argparse.ArgumentParser(prog=sys.argv[0])
    parser.add_argument('--build-args', type=str, default=None,
                        help='build arguments to pass')
    remote.add_common_args(parser)

    args = parser.parse_args()

    remote.load_profile(args, args.profile)
    remote.apply_default_arg_values(args)
    remote.log_args(args)

    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    escaped_remote_path = remote.sync_changes_with_args(args)

    if args.skip_build:
        sys.exit(0)

    remote_args = []
    if args.build_type is not None:
        remote_args.append("--build={}".format(args.build_type))
    remote_args.append("--force")
    if args.build_args is not None:
        remote_args.append("--build_args=\"{}\"".format(args.build_args))

    remote.exec_command(
        host=args.host,
        escaped_remote_path=escaped_remote_path,
        script_name='yb_release',
        script_args=remote_args,
        should_quote_args=False,
        extra_ssh_args=remote.process_extra_ssh_args(args.extra_ssh_args))


if __name__ == '__main__':
    main()
