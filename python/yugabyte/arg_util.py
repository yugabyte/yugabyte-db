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

import argparse


from yugabyte import git_util


def sha1_regex_arg_type(arg_value: str) -> str:
    arg_value = arg_value.strip().lower()
    if not git_util.SHA1_RE.match(arg_value):
        raise argparse.ArgumentTypeError(
            f"Invalid SHA1 value: {arg_value}, expected 40 hexadecimal digits, found "
            f"an argument of length {len(arg_value)}, after trimming whitespace.")
    return arg_value
