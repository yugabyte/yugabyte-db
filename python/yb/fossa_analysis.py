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

from yb.common_util import (
    YB_SRC_ROOT, get_thirdparty_dir, get_download_cache_dir, load_yaml_file, init_env, shlex_join,
    write_yaml_file
)

from downloadutil.downloader import Downloader
from downloadutil.download_config import DownloadConfig


FOSSA_VERSION_RE = re.compile(r'^fossa-cli version ([^ ]+) .*$')
MIN_FOSSA_CLI_VERSION = '1.1.7'


def should_include_fossa_module(name: str) -> bool:
    return not name.startswith(('llvm', 'gmock', 'cassandra-cpp-driver', 'bison', 'flex'))


def main():
    parser = argparse.ArgumentParser(
        description='Run FOSSA analysis (open source license compliance).')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose output')
    parser.add_argument(
        'fossa_cli_args',
        nargs='*',
        help='These arguments are passed directly to fossa-cli')
    args = parser.parse_args()
    init_env(args.verbose)

    fossa_cmd_line = ['fossa', 'analyze']
    fossa_cmd_line.extend(args.fossa_cli_args)

    should_upload = not any(
        arg in args.fossa_cli_args for arg in ('--show-output', '--output', '-o'))

    if not should_upload and not os.getenv('FOSSA_API_KEY'):
        # --output is used for local analysis only, without uploading the results. In all other
        # cases we would like .
        raise RuntimeError('FOSSA_API_KEY must be specified in order to upload analysis results.')

    logging.info(
        f"FOSSA CLI command line (except the configuration path): {shlex_join(fossa_cmd_line)}")

    fossa_version_str = subprocess.check_output(['fossa', '--version']).decode('utf-8')
    fossa_version_match = FOSSA_VERSION_RE.match(fossa_version_str)
    if not fossa_version_match:
        raise RuntimeError(f"Cannot parse fossa-cli version: {fossa_version_str}")
    fossa_version = fossa_version_match.group(1)
    if version.parse(fossa_version) < version.parse(MIN_FOSSA_CLI_VERSION):
        raise RuntimeError(
            f"fossa-cli version too old: {fossa_version} "
            f"(expected {MIN_FOSSA_CLI_VERSION} or later)")

    download_cache_path = get_download_cache_dir()
    logging.info(f"Using the download cache directory {download_cache_path}")
    download_config = DownloadConfig(
        verbose=args.verbose,
        cache_dir_path=download_cache_path
    )
    downloader = Downloader(download_config)

    fossa_yml_path = os.path.join(YB_SRC_ROOT, '.fossa.yml')
    fossa_yml_data = load_yaml_file(fossa_yml_path)
    modules = fossa_yml_data['analyze']['modules']

    thirdparty_dir = get_thirdparty_dir()
    fossa_modules_path = os.path.join(thirdparty_dir, 'fossa_modules.yml')

    seen_urls = set()

    start_time_sec = time.time()
    if os.path.exists(fossa_modules_path):
        thirdparty_fossa_modules_data = load_yaml_file(fossa_modules_path)
        for thirdparty_module_data in thirdparty_fossa_modules_data:
            fossa_module_data = thirdparty_module_data['fossa_module']
            module_name = fossa_module_data['name']
            if not should_include_fossa_module(module_name):
                continue
            fossa_module_yb_metadata = thirdparty_module_data['yb_metadata']
            expected_sha256 = fossa_module_yb_metadata['sha256sum']
            url = fossa_module_yb_metadata['url']
            if url in seen_urls:
                # Due to a bug in some versions of yugabyte-db-thirdparty scripts, as of 04/20/2021
                # we may include the same dependency twince in the fossa_modules.yml file. We just
                # skip the duplicates here.
                continue
            seen_urls.add(url)

            logging.info(f"Adding module from {url}")
            downloaded_path = downloader.download_url(
                url,
                download_parent_dir_path=None,  # Download to cache directly.
                verify_checksum=True,
                expected_sha256=expected_sha256
            )
            fossa_module_data['target'] = downloaded_path
            modules.append(fossa_module_data)

        effective_fossa_yml_path = os.path.join(os.getenv('BUILD_ROOT'), 'effective_fossa.yml')
        write_yaml_file(fossa_yml_data, effective_fossa_yml_path)

        logging.info(f"Wrote the expanded FOSSA file to {effective_fossa_yml_path}")
    else:
        logging.warning(
            f"File {fossa_modules_path} does not exist. Some C/C++ dependencies will be missing "
            f"from FOSSA analysis.")

        effective_fossa_yml_path = fossa_yml_path

    fossa_cmd_line += ['--config', effective_fossa_yml_path]

    elapsed_time_sec = time.time() - start_time_sec
    logging.info("Generated the effective FOSSA configuration file in %.1f sec", elapsed_time_sec)
    logging.info(f"Running command: {shlex_join(fossa_cmd_line)})")
    subprocess.check_call(fossa_cmd_line)


if __name__ == '__main__':
    main()
