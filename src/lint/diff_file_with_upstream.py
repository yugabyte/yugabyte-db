#!/usr/bin/env python3
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
#
# Diff a file with upstream's version of it.
# Input: filepath (with respect to yugabyte/yugabyte-db repository) to run diff for.
# - Exit 0 for successfully getting a diff (regardless of whether there are differences or not).
#   Output the diff to stdout.
# - Exit 1 for exceptions and assertion failures.  For the most part, output the main error message
#   to stdout.  stderr gets the full stack trace that Python naturally provides.
# - Exit 2 when the upstream file or commit is not found.
# If you get exit code 1, you may need to update the list of expected exceptions to be caught.

import subprocess
import sys

import upstream_file


def main(filepath, *diff_options):
    try:
        contents = upstream_file.fetch(filepath)
    except upstream_file.NotFoundError:
        return 2
    except upstream_file.UnpinnedError:
        return 0

    p = subprocess.Popen(["diff", filepath, "-"] + list(diff_options),
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE)
    output = p.communicate(contents)[0]
    if p.returncode in (0, 1):
        sys.stdout.buffer.write(output)
        return 0

    raise ValueError(f"Failed to diff for {filepath}")


if __name__ == "__main__":
    exit(main(*sys.argv[1:]))
