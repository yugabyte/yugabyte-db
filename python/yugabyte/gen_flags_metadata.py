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

import argparse
import os
import logging
import time

from yugabyte_pycommon import run_program, WorkDirContext  # type: ignore

from yugabyte.common_util import init_logging


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--program_name', type=str, help="Program name")
    parser.add_argument('--dynamically_linked_exe_suffix', type=str,
                        help="Executable suffix used in case of dynamic linking")
    parser.add_argument('--output_file_path', type=str, help="Path to output file")
    args = parser.parse_args()

    start_time_sec = time.time()
    build_root = os.environ['YB_BUILD_ROOT']

    with WorkDirContext(build_root):
        content = run_program(
                    [
                        os.path.join(build_root, 'bin', args.program_name),
                        "--dump_flags_xml",
                        "--dynamically_linked_exe_suffix", args.dynamically_linked_exe_suffix,
                    ],
                    shell=True
                )

        with open(args.output_file_path, 'w+', encoding='utf-8') as f:
            f.write(content.stdout)

    elapsed_time_sec = time.time() - start_time_sec
    logging.info("Generated flags_metadata for %s in %.1f sec", args.program_name, elapsed_time_sec)


if __name__ == '__main__':
    init_logging(verbose=False)
    main()
