#!/usr/bin/env python3

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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
# This script generates a header file which contains definitions
# for the current YugaByte build (e.g. timestamp, git hash, etc)

import argparse
import json
import logging
import os
import platform
import pwd
import re
import shlex
import socket
import subprocess
import sys
import time

from typing import Optional

from time import strftime, localtime

# Go from "${YB_SRC_ROOT}/python/yugabyte/gen_version_info.py? to "${YB_SRC_ROOT}/python".
sys.path.append(os.path.dirname(os.path.dirname(__file__)))


from yugabyte.common_util import get_yb_src_root_from_build_root  # noqa
from yugabyte import git_util  # noqa


def is_git_repo_clean(git_repo_dir: str) -> bool:
    return subprocess.call(
        "cd {} && git diff --quiet && git diff --cached --quiet".format(
            shlex.quote(git_repo_dir)),
        shell=True) == 0


def boolean_to_json_str(bool_flag: bool) -> str:
    return str(bool_flag).lower()


def get_glibc_version() -> str:
    glibc_v = subprocess.check_output('ldd --version', shell=True).decode('utf-8').strip()
    # We only want the version
    return glibc_v.split("\n")[0].split()[-1]


def get_git_sha1(git_repo_dir: str) -> Optional[str]:
    try:
        sha1 = subprocess.check_output(
            'cd {} && git rev-parse HEAD'.format(shlex.quote(git_repo_dir)), shell=True
        ).decode('utf-8').strip()

        if git_util.SHA1_RE.match(sha1):
            return sha1
        logging.warning("Invalid git SHA1 in directory '%s': %s", git_repo_dir, sha1)
        return None

    except Exception as e:
        logging.warning("Failed to get git SHA1 in directory: %s", git_repo_dir)
        return None


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    parser = argparse.ArgumentParser(usage="usage: gen_version_info.py <output_path>")
    parser.add_argument("--build-type", help="Set build type", type=str)
    parser.add_argument("--git-hash", help="Set git hash", type=str)
    parser.add_argument("output_path", help="Output file to be generated.", type=str)
    args = parser.parse_args()

    output_path = args.output_path

    hostname = socket.gethostname()
    build_time = "%s %s" % (strftime("%d %b %Y %H:%M:%S", localtime()), time.tzname[0])

    username: Optional[str]
    try:
        username = os.getlogin()
    except OSError as ex:
        logging.warning(("Got an OSError trying to get the current user name, " +
                         "trying a workaround: {}").format(ex))
        # https://github.com/gitpython-developers/gitpython/issues/39
        try:
            username = pwd.getpwuid(os.getuid()).pw_name
        except KeyError:
            username = os.getenv('USER')
            if not username:
                id_output = subprocess.check_output('id').decode('utf-8').strip()
                ID_OUTPUT_RE = re.compile(r'^uid=\d+[(]([^)]+)[)]\s.*')
                match = ID_OUTPUT_RE.match(id_output)
                if match:
                    username = match.group(1)
                else:
                    logging.warning(
                        "Couldn't get user name from the environment, either parse 'id' output: %s",
                        id_output)
                    raise

    git_repo_dir = get_yb_src_root_from_build_root(os.getcwd(), must_succeed=False)
    if git_repo_dir:
        clean_repo = is_git_repo_clean(git_repo_dir)
    else:
        clean_repo = False

    git_hash: Optional[str]
    if args.git_hash:
        # Git hash provided on the command line.
        git_hash = args.git_hash
    elif 'YB_VERSION_INFO_GIT_SHA1' in os.environ:
        git_hash = os.environ['YB_VERSION_INFO_GIT_SHA1']
        logging.info("Git SHA1 provided using the YB_VERSION_INFO_GIT_SHA1 env var: %s", git_hash)
    elif git_repo_dir:
        # No command line git hash, find it in the local git repository.
        git_hash = get_git_sha1(git_repo_dir)
    else:
        git_hash = None

    path_to_version_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)), "..", "..", "version.txt")
    with open(path_to_version_file) as version_file:
        version_string = version_file.read().strip()
    match = re.match(r"(\d+\.\d+\.\d+\.\d+)", version_string)
    if not match:
        parser.error("Invalid version specified: {}".format(version_string))
        sys.exit(1)
    version_number = match.group(1)
    build_type = args.build_type

    # The minimum YBA version required by this build.  If there is no min_yba_version.txt then
    # default to 0.0.0 (yba uses semvar).
    path_to_yba_min_version_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)), "..", "..", "min_yba_version.txt")
    if os.path.isfile(path_to_yba_min_version_file):
        with open(path_to_yba_min_version_file) as version_file:
            min_yba_version = version_file.read().strip()
    else:
        min_yba_version = '0.0.0.0'

    # Add the Jenkins build ID
    build_id = os.getenv("BUILD_ID", "")
    # This will be replaced by the release process.
    build_number = os.getenv("YB_RELEASE_BUILD_NUMBER") or "PRE_RELEASE"

    # Fetch system platform and architecture
    os_platform = sys.platform
    architecture = platform.machine()

    d = os.path.dirname(output_path)
    if d != "" and not os.path.exists(d):
        os.makedirs(d)
    log_file_path = os.path.join(d, os.path.splitext(os.path.basename(__file__))[0] + '.log')
    file_log_handler = logging.FileHandler(log_file_path)
    file_log_handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s: %(message)s"))
    logging.getLogger('').addHandler(file_log_handler)

    data = {
            "schema": "v1",
            "git_hash": git_hash,
            "build_hostname": hostname,
            "build_timestamp": build_time,
            "build_username": username,
            # In version_info.cc we expect build_clean_repo to be a "true"/"false" string.
            "build_clean_repo": boolean_to_json_str(clean_repo),
            "build_id": build_id,
            "build_type": build_type,
            "version_number": version_number,
            "build_number": build_number,
            "platform": os_platform,
            "architecture": architecture,
            "minimum_yba_version": min_yba_version
            }

    # Record our glibc version.  This doesn't apply to mac/darwin.
    if os_platform == 'linux':
        data["glibc_v"] = get_glibc_version()

    content = json.dumps(data)

    # Frequently getting errors here when rebuilding on NFS:
    # https://gist.githubusercontent.com/mbautin/572dc0ab6b9c269910c1a51f31d79b38/raw
    attempts_left = 10
    while attempts_left > 0:
        try:
            with open(output_path, "w") as f:
                f.write(content)
            break
        except IOError as ex:
            if attempts_left == 0:
                raise ex
            if 'Resource temporarily unavailable' in str(ex):
                time.sleep(0.1)
        attempts_left -= 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
