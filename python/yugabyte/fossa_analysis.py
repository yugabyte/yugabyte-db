#!/usr/bin/env python3
# Copyright (c) Yugabyte, Inc.
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

import os
import subprocess
import argparse
import tempfile
import atexit
import logging
import shlex
import packaging
import re
import time

from packaging import version

from yugabyte.common_util import (
    YB_SRC_ROOT, get_thirdparty_dir, load_yaml_file, shlex_join, write_yaml_file, init_logging
)


FOSSA_VERSION_RE = re.compile(r'^(?:fossa-cli|spectrometer:) version ([^ ]+) .*$')
MIN_FOSSA_CLI_VERSION = '2.15.19'


def should_include_fossa_module(name: str) -> bool:
    return not name.startswith(('llvm', 'gmock', 'cassandra-cpp-driver', 'bison', 'flex'))


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Run FOSSA analysis (open source license compliance).')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose output')
    parser.add_argument(
        'fossa_cli_args',
        nargs='*',
        help='These arguments are passed directly to fossa-cli')
    args = parser.parse_args()
    init_logging(verbose=args.verbose)

    # TODO: We may also want to try using the v2 option --unpack-archives
    #       Though that may be going to deeper level than we want.
    fossa_cmd_line = ['fossa', 'analyze']
    fossa_cmd_line.extend(args.fossa_cli_args)

    should_upload = not any(
        arg in args.fossa_cli_args for arg in ('--show-output', '--output', '-o'))

    if should_upload and not os.getenv('FOSSA_API_KEY'):
        # --output is used for local analysis only, without uploading the results. In all other
        # cases we would like .
        raise RuntimeError('FOSSA_API_KEY must be specified in order to upload analysis results.')

    logging.info(
        f"FOSSA CLI command line: {shlex_join(fossa_cmd_line)}")

    fossa_version_str = subprocess.check_output(['fossa', '--version']).decode('utf-8')
    fossa_version_match = FOSSA_VERSION_RE.match(fossa_version_str)
    if not fossa_version_match:
        raise RuntimeError(f"Cannot parse fossa-cli version: {fossa_version_str}")
    fossa_version = fossa_version_match.group(1)
    if version.parse(fossa_version) < version.parse(MIN_FOSSA_CLI_VERSION):
        raise RuntimeError(
            f"fossa version too old: {fossa_version} "
            f"(expected {MIN_FOSSA_CLI_VERSION} or later)")

    fossa_yml_path = os.path.join(YB_SRC_ROOT, '.fossa-local.yml')
    fossa_yml_data = load_yaml_file(fossa_yml_path)

    #####################
    # Removed logic to pull in thirdparty modules for fossa analysis.
    # v3 does not handle module definitions the same way.
    # Analysis of thirdparty dependencies will be revisited, and maybe done separately.
    #####################

    start_time_sec = time.time()
    effective_fossa_yml_path = os.path.join(YB_SRC_ROOT, '.fossa.yml')
    write_yaml_file(fossa_yml_data, effective_fossa_yml_path)

    logging.info(f"Wrote the {fossa_yml_path} FOSSA file to {effective_fossa_yml_path}")
    elapsed_time_sec = time.time() - start_time_sec
    logging.info("Generated the effective FOSSA configuration file in %.1f sec", elapsed_time_sec)
    logging.info(f"Running command: {shlex_join(fossa_cmd_line)})")
    subprocess.check_call(fossa_cmd_line)

    # Platform project
    fossa_yml_plat_path = os.path.join(YB_SRC_ROOT, '.fossa-platform.yml')
    fossa_yml_plat_data = load_yaml_file(fossa_yml_plat_path)
    write_yaml_file(fossa_yml_plat_data, effective_fossa_yml_path)
    logging.info(f"Wrote the {fossa_yml_plat_path} FOSSA file to {effective_fossa_yml_path}")
    logging.info(f"Running command: {shlex_join(fossa_cmd_line)})")
    subprocess.check_call(fossa_cmd_line)


if __name__ == '__main__':
    main()
