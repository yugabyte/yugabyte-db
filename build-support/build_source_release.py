#!/usr/bin/env python

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

import hashlib
import logging
import os
import subprocess
import sys

from kudu_util import check_output, confirm_prompt, Colors, get_my_email

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GET_UPSTREAM_COMMIT_SCRIPT = os.path.join(ROOT, "build-support", "get-upstream-commit.sh")


def check_repo_not_dirty():
  """Check that the git repository isn't dirty."""
  dirty_repo = subprocess.call("git diff --quiet && git diff --cached --quiet",
                               shell=True) != 0
  if not dirty_repo:
    return
  print "The repository does not appear to be clean."
  print Colors.RED + "The source release will not include your local changes." + \
      Colors.RESET
  if not confirm_prompt("Continue?"):
    sys.exit(1)


def check_no_local_commits():
  """
  Check that there are no local commits which haven't been pushed to the upstream
  repo via Jenkins.
  """
  upstream_commit = check_output(GET_UPSTREAM_COMMIT_SCRIPT).strip()
  cur_commit = check_output(["git", "rev-parse", "HEAD"]).strip()

  if upstream_commit == cur_commit:
    return
  print "The repository appears to have local commits:"
  subprocess.check_call(["git", "log", "--oneline", "%s..HEAD" % upstream_commit])

  print Colors.RED + "This should not be an official release!" + \
      Colors.RESET
  if not confirm_prompt("Continue?"):
    sys.exit(1)


def get_version_number():
  """ Return the current version number of Kudu. """
  return file(os.path.join(ROOT, "version.txt")).read().strip()


def create_tarball():
  artifact_name = "apache-kudu-incubating-%s" % get_version_number()
  build_dir = os.path.join(ROOT, "build")
  if not os.path.exists(build_dir):
    os.path.makedirs(build_dir)
  tarball_path = os.path.join(build_dir, artifact_name + ".tar.gz")
  print "Exporting source tarball..."
  subprocess.check_output(["git", "archive",
                           "--prefix=%s/" % artifact_name,
                           "--output=%s" % tarball_path,
                           "HEAD"])
  print Colors.GREEN + "Generated tarball:\t" + Colors.RESET, tarball_path
  return tarball_path


def sign_tarball(tarball_path):
  """ Prompt the user to GPG-sign the tarball using their Apache GPG key. """
  if not confirm_prompt("Would you like to GPG-sign the tarball now?"):
    return

  email = get_my_email()
  if not email.endswith("@apache.org"):
    print Colors.YELLOW, "Your email address for the repository is not an @apache.org address."
    print "Release signatures should typically be signed by committers with @apache.org GPG keys."
    print Colors.RESET,
    if not confirm_prompt("Continue?"):
      return

  try:
    subprocess.check_call(["gpg", "--detach-sign", "--armor", "-u", email, tarball_path])
  except subprocess.CalledProcessError:
    print Colors.RED + "GPG signing failed. Artifact will not be signed." + Colors.RESET
    return
  print Colors.GREEN + "Generated signature:\t" + Colors.RESET, tarball_path + ".asc"


def checksum_file(summer, path):
  """
  Calculates the checksum of the file 'path' using the provided hashlib
  digest implementation. Returns the hex form of the digest.
  """
  with file(path, "rb") as f:
    # Read the file in 4KB chunks until EOF.
    for chunk in iter(lambda: f.read(4096), ""):
      summer.update(chunk)
  return summer.hexdigest()


def gen_checksum_files(tarball_path):
  """
  Create md5 and sha files of the tarball.

  The output format is compatible with command line tools like 'sha1sum'
  and 'md5sum' so they may be used to verify the checksums.
  """
  hashes = [(hashlib.sha1, "sha"),
            (hashlib.md5, "md5")]
  for hash_func, extension in hashes:
    digest = checksum_file(hash_func(), tarball_path)
    path = tarball_path + "." + extension
    with file(path, "w") as f:
      print >>f, "%s\t%s" % (digest, os.path.basename(tarball_path))
    print Colors.GREEN + ("Generated %s:\t" % extension) + Colors.RESET, path


def main():
  # Change into the source repo so that we can run git commands without having to
  # specify cwd=BUILD_SUPPORT every time.
  os.chdir(ROOT)
  check_repo_not_dirty()
  check_no_local_commits()
  tarball_path = create_tarball()
  gen_checksum_files(tarball_path)
  sign_tarball(tarball_path)

  print Colors.GREEN + "Release successfully generated!" + Colors.RESET
  print


if __name__ == "__main__":
  logging.basicConfig(level=logging.INFO)
  main()
