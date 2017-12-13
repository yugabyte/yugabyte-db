#!/usr/bin/env python
#
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
from time import strftime, localtime


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
        try:
            # No command line git hash, find it in the local git repository.
            git_hash = subprocess.check_output(["git", "rev-parse", "HEAD"]).strip()
            clean_repo = subprocess.call(
                "git diff --quiet && git diff --cached --quiet", shell=True) == 0
            clean_repo = str(clean_repo).lower()
        except Exception, e:
            # If the git commands failed, we're probably building outside of a git
            # repository.
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

    with file(output_path, "w") as f:
        print >>f, content

    return 0


if __name__ == "__main__":
    sys.exit(main())
