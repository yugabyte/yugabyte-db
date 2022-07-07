#!/usr/bin/env python

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

"""
A wrapper to generate gFlags metadata for yb-master and yb-tserver.
"""

import sys
import os
import logging
import time

from yugabyte_pycommon import init_logging, run_program, WorkDirContext  # type: ignore


def main():
    if len(sys.argv) != 3:
        print("Usage: {} <program_name> <output_file_path>".format(sys.argv[0]))
        sys.exit(1)
    start_time_sec = time.time()
    build_root = os.environ['YB_BUILD_ROOT']
    program_name = sys.argv[1]
    flags_metadata_file = sys.argv[2]

    with WorkDirContext(build_root):
        content = run_program(
                    [
                        os.path.join(build_root, 'bin', program_name),
                        "--dump_flags_xml",
                    ],
                    shell=True
                )

        with open(flags_metadata_file, 'w+', encoding='utf-8') as f:
            f.write(content.stdout)

    elapsed_time_sec = time.time() - start_time_sec
    logging.info("Generated flags_metadata for %s in %.1f sec", program_name, elapsed_time_sec)


if __name__ == '__main__':
    init_logging()
    main()
