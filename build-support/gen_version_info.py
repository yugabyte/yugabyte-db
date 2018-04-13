#!/usr/bin/env python2

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

import json
import logging
import argparse
import os
import re
import sha
import subprocess
import sys
import time
import pipes
from time import strftime, localtime

sys.path.append(os.path.join(os.path.dirname(os.path.dirname(__file__)), 'python'))


from yb.common_util import is_yugabyte_git_repo_dir  # noqa


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    parser = argparse.ArgumentParser(usage="usage: %prog <output_path>")
    parser.add_argument("--build-type", help="Set build type", type=str)
    parser.add_argument("--git-hash", help="Set git hash", type=str)
    parser.add_argument("output_path", help="Output file to be generated.", type=str)
    args = parser.parse_args()

    output_path = args.output_path

    hostname = subprocess.check_output(["hostname", "-f"]).strip()
    build_time = "%s %s" % (strftime("%d %b %Y %H:%M:%S", localtime()), time.tzname[0])
    username = os.getenv("USER")

    if args.git_hash:
        # Git hash provided on the command line.
        git_hash = args.git_hash
        clean_repo = "true"
    else:
        # No command line git hash, find it in the local git repository.

        # Handle the "external build" directory case, in which the code is located in e.g.
        # ~/code/yugabyte, and the build directories are inside ~/code/yugabyte__build.
        current_dir = os.getcwd()
        git_repo_dir = None
        while current_dir != '/':
            subdir_candidates = [current_dir]
            if current_dir.endswith('__build'):
                subdir_candidates.append(current_dir[:-7])
                if subdir_candidates[-1].endswith('/'):
                    subdir_candidates = subdir_candidates[:-1]

            for git_subdir_candidate in subdir_candidates:
                git_dir_candidate = os.path.join(current_dir, git_subdir_candidate)
                if is_yugabyte_git_repo_dir(git_dir_candidate):
                    git_repo_dir = git_dir_candidate
                    break
            if git_repo_dir:
                break
            current_dir = os.path.dirname(current_dir)
        if git_repo_dir:
            logging.info("Found git repository root: %s", git_repo_dir)
        else:
            git_repo_dir = os.getcwd()
            logging.warn("Could not find git repository root by walking up from %s", git_repo_dir)

        try:
            git_work_dir_quoted = pipes.quote(git_repo_dir)
            git_hash = subprocess.check_output('cd {} && git rev-parse HEAD'.format(
                git_work_dir_quoted), shell=True).strip()
            clean_repo = subprocess.call(
                "cd {} && git diff --quiet && git diff --cached --quiet".format(
                    git_work_dir_quoted),
                shell=True) == 0
            clean_repo = str(clean_repo).lower()
        except Exception, e:
            # If the git commands failed, we're probably building outside of a git repository.
            logging.info("Build appears to be outside of a git repository... " +
                         "continuing without repository information.")
            git_hash = "non-git-build"
            clean_repo = "true"

    path_to_version_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)), "..", "version.txt")
    version_string = file(path_to_version_file).read().strip()
    match = re.match("(\d+\.\d+\.\d+\.\d+)", version_string)
    if not match:
        parser.error("Invalid version specified: {}".format(version_string))
        sys.exit(1)
    version_number = match.group(1)
    build_type = args.build_type

    # Add the Jenkins build ID
    build_id = os.getenv("BUILD_ID", "")
    # This will be replaced by the release process.
    build_number = "PRE_RELEASE"

    d = os.path.dirname(output_path)
    if not os.path.exists(d):
        os.makedirs(d)
    log_file_path = os.path.join(d, os.path.splitext(os.path.basename(__file__))[0] + '.log')
    file_log_handler = logging.FileHandler(log_file_path)
    file_log_handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s: %(message)s"))
    logging.getLogger('').addHandler(file_log_handler)

    data = {
            "git_hash": git_hash,
            "build_hostname": hostname,
            "build_timestamp": build_time,
            "build_username": username,
            "build_clean_repo": clean_repo,
            "build_id": build_id,
            "build_type": build_type,
            "version_number": version_number,
            "build_number": build_number
            }
    content = json.dumps(data)

    # Frequently getting errors here when rebuilding on NFS:
    # https://gist.githubusercontent.com/mbautin/572dc0ab6b9c269910c1a51f31d79b38/raw
    attempts_left = 10
    while attempts_left > 0:
        try:
            with file(output_path, "w") as f:
                print >>f, content
            break
        except IOError, ex:
            if attempts_left == 0:
                raise ex
            if 'Resource temporarily unavailable' in ex.message:
                time.sleep(0.1)
        attempts_left -= 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
