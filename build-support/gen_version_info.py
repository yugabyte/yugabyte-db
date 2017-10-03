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
# This script generates a header file which contains definitions
# for the current Kudu build (eg timestamp, git hash, etc)

import logging
import optparse
import os
import re
import sha
import subprocess
import sys
import time
from time import strftime, localtime

from kudu_util import check_output

def output_up_to_date(path, id_hash):
  """
  Return True if the old file seems to be up to date, based on the
  identification hash 'id_hash'.
  """
  if not os.path.exists(path):
    return False
  f = file(path).read()
  m = re.search("id_hash=(\w+)", f)
  if not m:
    return False
  return m.group(1) == id_hash

def main():
  logging.basicConfig(level=logging.INFO)
  parser = optparse.OptionParser(
      usage="usage: %prog --version=<version> <output path>")
  parser.add_option("-v", "--version", help="Set version number", type="string",
                    dest="version", metavar="VERSION")
  parser.add_option("-b", "--build-type", help="Set build type", type="string",
                    dest="build_type", metavar="BUILD_TYPE")
  parser.add_option("-g", "--git-hash", help="Set git hash", type="string",
                    dest="git_hash", metavar="GIT_HASH")
  opts, args = parser.parse_args()

  if not opts.version:
    parser.error("no version number specified")
    sys.exit(1)

  if len(args) != 1:
    parser.error("no output path specified")
    sys.exit(1)

  output_path = args[0]

  hostname = check_output(["hostname", "-f"]).strip()
  build_time = "%s %s" % (strftime("%d %b %Y %H:%M:%S", localtime()), time.tzname[0])
  username = os.getenv("USER")

  if opts.git_hash:
    # Git hash provided on the command line.
    git_hash = opts.git_hash
    clean_repo = "true"
  else:
    try:
      # No command line git hash, find it in the local git repository.
      git_hash = check_output(["git", "rev-parse", "HEAD"]).strip()
      clean_repo = subprocess.call("git diff --quiet && git diff --cached --quiet", shell=True) == 0
      clean_repo = str(clean_repo).lower()
    except Exception, e:
      # If the git commands failed, we're probably building outside of a git
      # repository.
      logging.info("Build appears to be outside of a git repository... " +
                   "continuing without repository information.")
      git_hash = "non-git-build"
      clean_repo = "true"

  version_string = opts.version
  build_type = opts.build_type

  # Add the Jenkins build ID
  build_id = os.getenv("BUILD_ID", "")

  # Calculate an identifying hash based on all of the variables except for the
  # timestamp. We put this hash in a comment, and use it to check whether to
  # re-generate the file. If it hasn't changed since a previous run, we don't
  # re-write the file. This avoids having to rebuild all binaries on every build.
  identifying_hash = sha.sha(repr((git_hash, hostname, username,
                                   clean_repo, build_id))).hexdigest()

  if output_up_to_date(output_path, identifying_hash):
    return 0
  d = os.path.dirname(output_path)
  if not os.path.exists(d):
    os.makedirs(d)
  with file(output_path, "w") as f:
    print >>f, """
// THIS FILE IS AUTO-GENERATED! DO NOT EDIT!
//
// id_hash=%(identifying_hash)s
#ifndef VERSION_INFO_H_
#define VERSION_INFO_H_

#define KUDU_GIT_HASH "%(git_hash)s"
#define KUDU_BUILD_HOSTNAME "%(hostname)s"
#define KUDU_BUILD_TIMESTAMP "%(build_time)s"
#define KUDU_BUILD_USERNAME "%(username)s"
#define KUDU_BUILD_CLEAN_REPO %(clean_repo)s
#define KUDU_BUILD_ID "%(build_id)s"
#define KUDU_BUILD_TYPE "%(build_type)s"
#define KUDU_VERSION_STRING "%(version_string)s"
#endif
""" % locals()
  return 0

if __name__ == "__main__":
  sys.exit(main())
