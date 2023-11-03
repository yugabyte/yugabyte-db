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
from yugabyte_pycommon import run_program, WorkDirContext  # type: ignore

from yugabyte.common_util import init_logging


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


def is_json_subset(a: Dict[str, str], b: Dict[str, str]) -> bool:
    subset = {}
    for k, v in a.items():
        if k in b:
            subset[k] = b[k]
    return subset == a


def check_auto_flags_subset(
        previous_auto_flags: Dict[str, List[Dict[str, List[Dict[str, str]]]]],
        new_auto_flags: Dict[str, List[Dict[str, List[Dict[str, str]]]]]) -> None:
    """
    Once a AutoFlag is added to the code and shipped to customers it cannot be removed.
    Check that the new AutoFlags JSON is a subset of the previous stabilized AutoFlags JSON.
    """
    for previous_program_flags in previous_auto_flags['auto_flags']:
        program_validated = False
        for program_flags in new_auto_flags['auto_flags']:
            if program_flags['program'] != previous_program_flags['program']:
                continue
            else:
                for previous_flag in previous_program_flags['flags']:
                    flag_validated = False
                    for flag in program_flags['flags']:
                        if is_json_subset(previous_flag, flag):
                            flag_validated = True
                            break

                    if not flag_validated:
                        raise RuntimeError("AutoFlag {} in {} has been removed".format(
                            previous_flag,
                            previous_program_flags['program']))

                program_validated = True
                break

        if not program_validated:
            raise RuntimeError("AutoFlags for program {} has been removed".format(
                previous_program_flags['program']))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--program_list', type=str, help="CSV list of programs")
    parser.add_argument('--dynamically_linked_exe_suffix', type=str,
                        help="Executable suffix used in case of dynamic linking")
    parser.add_argument('--output_file_path', type=str, help="Path to output file")
    parser.add_argument('--previous_auto_flags_file',
                        type=str,
                        help="Path to previous AutoFlags file")
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

        auto_flags_json: Dict[str, List[Dict[str, List[Dict[str, str]]]]] = {}
        auto_flags_json['auto_flags'] = []
        for flags in return_dict.values():
            auto_flags_json['auto_flags'] += [json.loads(flags)]

        with open(args.previous_auto_flags_file, 'r', encoding='utf-8') as previous_auto_flags_file:
            previous_auto_flags = json.load(previous_auto_flags_file)
            check_auto_flags_subset(previous_auto_flags, auto_flags_json)

        with open(args.output_file_path, 'w', encoding='utf-8') as new_auto_flags_file:
            json.dump(auto_flags_json, new_auto_flags_file, ensure_ascii=False, indent=2)
            new_auto_flags_file.write("\n")

        elapsed_time_sec = time.time() - start_time_sec
        logging.info("Generated AutoFlags JSON in %.1f sec" % elapsed_time_sec)


if __name__ == '__main__':
    init_logging(verbose=False)
    main()
