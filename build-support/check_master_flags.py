#!/usr/bin/env python3
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

"""
Fails if yb-master lacks a gflag the C++ master had.

The baseline (src/yb/master/master_flags_baseline.txt) lists the non-test, non-deprecated gflags of
the C++ master, which the Rust master replaced. A command line or a flagfile written for that master
must keep parsing, so yb-master has to define every one of them. To retire a flag, turn it into a
DEPRECATE_FLAG and remove it from the baseline.

Usage: check_master_flags.py --baseline <file> --binary <path to yb-master>
"""

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()

    with open(args.baseline) as f:
        baseline = {line.strip() for line in f if line.strip() and not line.startswith('#')}
    helpxml = subprocess.run(
        [args.binary, '--helpxml'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        check=True).stdout
    defined = {flag.findtext('name') for flag in ET.fromstring(helpxml).iter('flag')}

    missing = sorted(baseline - defined)
    if missing:
        sys.stderr.write(
            'yb-master is missing %d gflag(s) of the C++ master listed in %s:\n  %s\n'
            'Define them (see src/yb/master/master_flags_*.cc), or deprecate them with '
            'DEPRECATE_FLAG and remove them from the baseline.\n' %
            (len(missing), args.baseline, '\n  '.join(missing)))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
