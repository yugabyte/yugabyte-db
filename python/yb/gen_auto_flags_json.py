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
A wrapper to generate AutoFlags JSON for yb-master and yb-tserver.
"""

import argparse
import json
import logging
import multiprocessing
import os
import sys
import time

from typing import List, Dict, cast
from yugabyte_pycommon import init_logging, run_program, WorkDirContext  # type: ignore


def get_auto_flags(
        build_root: str,
        program_name: str,
        dynamically_linked_exe_suffix: str,
        return_dict: Dict[str, str]) -> None:
    auto_flags_result = run_program(
                [
                    os.path.join(build_root, 'bin', program_name),
                    "--help_auto_flag_json",
                    "--dynamically_linked_exe_suffix", dynamically_linked_exe_suffix,
                ],
                shell=True
            )
    return_dict[program_name] = auto_flags_result.stdout


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--program_list', type=str, help="CSV list of programs")
    parser.add_argument('--dynamically_linked_exe_suffix', type=str,
                        help="Executable suffix used in case of dynamic linking")
    parser.add_argument('--output_file_path', type=str, help="Path to output file")
    args = parser.parse_args()

    start_time_sec = time.time()
    build_root = os.environ['YB_BUILD_ROOT']

    manager = multiprocessing.Manager()
    # manager.dict() returns DictProxy[Any, Any]
    return_dict: Dict[str, str] = cast(Dict[str, str], manager.dict())
    process_list = []
    with WorkDirContext(build_root):
        for program_name in args.program_list.split(','):
            process = multiprocessing.Process(target=get_auto_flags,
                                              args=(
                                                build_root,
                                                program_name,
                                                args.dynamically_linked_exe_suffix,
                                                return_dict))
            process_list.append(process)
            process.start()

        for process in process_list:
            process.join()

        for process in process_list:
            if process.exitcode:
                sys.exit(process.exitcode)

        auto_flags_json: Dict[str, List[Dict[str, str]]] = {}
        auto_flags_json['auto_flags'] = []
        for flags in return_dict.values():
            auto_flags_json['auto_flags'] += [json.loads(flags)]

        with open(args.output_file_path, 'w', encoding='utf-8') as f:
            json.dump(auto_flags_json, f, ensure_ascii=False, indent=2)

        elapsed_time_sec = time.time() - start_time_sec
        logging.info("Generated AutoFlags JSON in %.1f sec" % elapsed_time_sec)


if __name__ == '__main__':
    init_logging()
    main()
