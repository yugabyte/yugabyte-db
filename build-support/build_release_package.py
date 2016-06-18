#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

import glob
import hashlib
import logging
import os
import subprocess
import sys

from yb_util import check_output, confirm_prompt, Colors, get_my_email

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
  cur_commit = check_output(["git", "rev-parse", "HEAD"]).strip()
  artifact_name = "yb-server-%s.%s" % (get_version_number(), cur_commit)
  build_dir = os.path.join(ROOT, "build")
  if not os.path.exists(build_dir):
    os.path.makedirs(build_dir)
  tarball_path = os.path.join(build_dir, artifact_name + ".tar")

  print "Exporting release tarball"

  # List of items we want in the tarball.
  #
  # For each item, the first entry is directory to find the relevant entries, and the second entry
  # is the UNIX-compliant wild-card path specification relative to the first entry of the paths
  # we want under the directory. The breakdown of path components between the first and second
  # entry in each row is important in deciding at what path the items land inside the tar file.
  #
  # For example a row below of the form:
  #   ['build/latest/', 'bin/yb-admin']
  # will cause 'build/latest/bin/yb-admin' to be added as 'bin/yb-admin' to the tar file.
  #
  # The item can have an optional third entry which is the gnu tar's transform expression
  # that can be used to place a file at a path which is completely different than its current
  # path. For example, rocksdb libraries are in rocksdb-build/ but we would like them under lib/.
  #
  #
  manifest = [
    [ '', 'www'],
    [ 'build/latest', 'lib/*.so*'],
    [ 'build/latest', 'bin/yb-admin'],
    [ 'build/latest', 'bin/yb-pbc-dump'],
    [ 'build/latest', 'bin/yb-master'],
    [ 'build/latest', 'bin/yb-tserver'],
    [ 'build/latest', 'bin/yb-fs_dump'],
    [ 'build/latest', 'bin/yb-fs_lis'],
    [ 'build/latest', 'bin/yb-ysck'],
    [ 'build/latest', 'bin/yb_load_test_tool'],
    [ 'build/latest', 'rocksdb-build/*.so.*', 's,rocksdb-build/,lib/,'],
    [ 'thirdparty/installed', 'lib/curl*.so.*'],
    [ 'thirdparty/installed', 'lib/libev.so.*'],
    [ 'thirdparty/installed', 'lib/libunwind*.so.*'],
    [ 'thirdparty/installed', 'lib/libvmem.so.*'],
    [ 'thirdparty/installed', 'lib/libz.so.*'],
    [ 'thirdparty/installed-deps', 'lib/*.so.*'] ,
    [ 'thirdparty/installed-deps', 'bin/*']
  ]
      
  print "Create an empty tarball"
  subprocess.check_output(["tar",
                           "cvf",
                           tarball_path,
                           "--files-from",
                           "/dev/null"])

  for item in manifest:
    print("Adding %s/%s to tarball" % (item[0], item[1]))
    os.chdir(ROOT + "/" + item[0])
    path_transformation = []
    if len(item) == 3:
      path_transformation = ["--transform", item[2]];
    subprocess.check_output(["tar",
                             "rvf",
                             tarball_path] + 
                            path_transformation +
                            glob.glob(item[1]))

  # Compress the tarball.
  subprocess.check_output(["gzip", tarball_path])
  tarball_path = tarball_path + ".gz"
  print Colors.GREEN + "Generated tarball:\t" + Colors.RESET, tarball_path
  return tarball_path


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
  print "Base directory: " + ROOT
  tarball_path = create_tarball()
  gen_checksum_files(tarball_path)

  print Colors.GREEN + "Release successfully generated!" + Colors.RESET
  print


if __name__ == "__main__":
  logging.basicConfig(level=logging.INFO)
  main()
