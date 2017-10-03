#!/usr/bin/env python2
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
# This script runs on the distributed-test slave and acts
# as a wrapper around run-test.sh.
#
# The distributed testing system can't pass in environment variables
# to commands, so this takes some parameters, turns them into environment
# variables, and then executes the test wrapper.
#
# We also 'cat' the test log upon completion so that the test logs are
# uploaded by the test slave back.

import optparse
import os
import re
import shutil
import subprocess
import sys

ME = os.path.abspath(__file__)
ROOT = os.path.abspath(os.path.join(os.path.dirname(ME), ".."))

def is_elf_binary(path):
  """ Determine if the given path is an ELF binary (executable or shared library) """
  if not os.path.isfile(path) or os.path.islink(path):
    return False
  try:
    with file(path, "rb") as f:
      magic = f.read(4)
      return magic == "\x7fELF"
  except:
    # Ignore unreadable files
    return False

def fix_rpath_component(bin_path, path):
  """
  Given an RPATH component 'path' of the binary located at 'bin_path',
  fix the thirdparty dir to be relative to the binary rather than absolute.
  """
  rel_tp = os.path.relpath(os.path.join(ROOT, "thirdparty/"),
                           os.path.dirname(bin_path))
  path = re.sub(r".*thirdparty/", "$ORIGIN/"+rel_tp + "/", path)
  return path

def fix_rpath(path):
  """
  Fix the RPATH/RUNPATH of the binary located at 'path' so that
  the thirdparty/ directory is properly found, even though we will
  run the binary at a different path than it was originally built.
  """
  # Fetch the original rpath.
  p = subprocess.Popen(["chrpath", path],
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
  stdout, stderr = p.communicate()
  if p.returncode != 0:
    return
  rpath = re.search("R(?:UN)?PATH=(.+)", stdout.strip()).group(1)
  # Fix it to be relative.
  new_path = ":".join(fix_rpath_component(path, c) for c in rpath.split(":"))
  # Write the new rpath back into the binary.
  subprocess.check_call(["chrpath", "-r", new_path, path])

def fixup_rpaths(root):
  """
  Recursively walk the directory tree 'root' and fix the RPATH for any
  ELF files (binaries/libraries) that are found.
  """
  for dirpath, dirnames, filenames in os.walk(root):
    for f in filenames:
      p = os.path.join(dirpath, f)
      if is_elf_binary(p):
        fix_rpath(p)

def main():
  p = optparse.OptionParser(usage="usage: %prog [options] <test-name>")
  p.add_option("-e", "--env", dest="env", type="string", action="append",
               help="key=value pairs for environment variables",
               default=[])
  options, args = p.parse_args()
  if len(args) < 1:
    p.print_help(sys.stderr)
    sys.exit(1)
  test_exe = args[0]
  test_name, _ = os.path.splitext(os.path.basename(test_exe))
  test_dir = os.path.dirname(test_exe)

  env = os.environ.copy()
  for env_pair in options.env:
    (k, v) = env_pair.split("=", 1)
    env[k] = v

  # Fix the RPATHs of any binaries. During the build, we end up with
  # absolute paths from the build machine. This fixes the paths to be
  # binary-relative so that we can run it on the new location.
  #
  # It's important to do this rather than just putting all of the thirdparty
  # lib directories into $LD_LIBRARY_PATH below because we need to make sure
  # that non-TSAN-instrumented runtime tools (like 'llvm-symbolizer') do _NOT_
  # pick up the TSAN-instrumented libraries, whereas TSAN-instrumented test
  # binaries (like 'foo_test' or 'kudu-tserver') _DO_ pick them up.
  fixup_rpaths(os.path.join(ROOT, "build"))
  fixup_rpaths(os.path.join(ROOT, "thirdparty"))

  env['LD_LIBRARY_PATH'] = ":".join(
    [os.path.join(ROOT, "build/dist-test-system-libs/"),
     os.path.abspath(os.path.join(test_dir, "..", "lib"))])

  # GTEST_OUTPUT must be canonicalized and have a trailing slash for gtest to
  # properly interpret it as a directory.
  env['GTEST_OUTPUT'] = 'xml:' + os.path.abspath(
    os.path.join(test_dir, "..", "test-logs")) + '/'

  env['ASAN_SYMBOLIZER_PATH'] = os.path.join(ROOT, "thirdparty/installed/bin/llvm-symbolizer")
  rc = subprocess.call([os.path.join(ROOT, "build-support/run-test.sh")] + args,
                       env=env)
  sys.exit(rc)


if __name__ == "__main__":
  main()
