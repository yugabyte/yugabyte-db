#!/usr/bin/env python3

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

# Checks syntax of a Python script. Based on this StackOverflow question: https://goo.gl/cfZLMe.

import os
import sys
import traceback


def horizontal_line() -> None:
    sys.stderr.write("-" * 80 + "\n")


def main() -> None:
    filename = sys.argv[1]
    if not os.path.exists(filename):
        sys.stderr.write(f"Python file does not exist, cannot check syntax: {filename}\n")
        # Don't consider this an error.
        sys.exit(0)

    source = open(filename, 'r').read() + '\n'
    try:
        compile(source, filename, 'exec')
    except:  # noqa
        horizontal_line()
        sys.stderr.write("Syntax error in {}:\n\n".format(filename))
        traceback.print_exc()
        horizontal_line()
        sys.exit(1)


if __name__ == '__main__':
    main()
