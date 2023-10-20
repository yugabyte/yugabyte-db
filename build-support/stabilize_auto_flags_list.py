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

import logging
import os
import shutil
import subprocess
import sys
import ruamel.yaml  # type: ignore

sys.path.append(os.path.join(os.path.dirname(os.path.dirname(__file__)), 'python/yugabyte'))
from common_util import (
    YB_SRC_ROOT,
    create_temp_dir,
    init_logging,
    shlex_join,
)  # noqa: E402


def main() -> None:
    tmp_dir = create_temp_dir()
    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')

    build_cmd_list = [
        os.path.join(YB_SRC_ROOT, "yb_build.sh"),
        "--write-build-descriptor", build_desc_path,
        "--skip-java"
    ]
    logging.info("Running build step:\n%s", shlex_join(build_cmd_list))
    try:
        subprocess.run(build_cmd_list, capture_output=True, check=True)
    except subprocess.CalledProcessError as e:
        logging.exception(f"Failed to run build step:\n"
                          f"STDOUT: {e.stdout.decode('utf-8')}\n")
        sys.exit(1)

    if not os.path.exists(build_desc_path):
        raise IOError("The build script failed to generate build descriptor file at '{}'".format(
                build_desc_path))

    yaml = ruamel.yaml.YAML()
    with open(build_desc_path) as build_desc_file:
        build_desc = yaml.load(build_desc_file)

    logging.info("Updating AutoFlags json file")

    build_root = build_desc['build_root']
    auto_flags_json_path = os.path.join(build_root, 'auto_flags.json')
    previous_auto_flags_json_path = os.path.join(
        YB_SRC_ROOT,
        "src",
        "yb",
        "previous_auto_flags.json")
    shutil.copyfile(auto_flags_json_path, previous_auto_flags_json_path)


if __name__ == "__main__":
    init_logging(verbose=False)
    main()
