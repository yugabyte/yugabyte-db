#!/usr/bin/env python2.7

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


def horizontal_line():
    print >>sys.stderr, "-" * 80


if __name__ == '__main__':
    filename = sys.argv[1]
    if not os.path.exists(filename):
        print >>sys.stderr, "Python file does not exist, cannot check syntax: %s" % filename
        print >>sys.stderr, "This file might have been deleted as part of the latest commit."
        # Don't consider this an error.
        sys.exit(0)

    source = open(filename, 'r').read() + '\n'
    try:
        compile(source, filename, 'exec')
    except:  # noqa
        horizontal_line()
        print >>sys.stderr, "Syntax error in {}:\n".format(filename)
        traceback.print_exc()
        horizontal_line()
        sys.exit(1)
