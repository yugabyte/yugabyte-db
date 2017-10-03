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
# This tool allows tests to be submitted to a distributed testing
# service running on shared infrastructure.
#
# See dist_test.py --help for usage information.

import argparse
import glob
try:
  import simplejson as json
except:
  import json
import logging
import os
import pprint
import re
import sys
import shlex
import shutil
import subprocess
import time

TEST_TIMEOUT_SECS = int(os.environ.get('TEST_TIMEOUT_SECS', '900'))
ARTIFACT_ARCHIVE_GLOBS = ["build/*/test-logs/*"]
ISOLATE_SERVER = os.environ.get('ISOLATE_SERVER',
                                "http://isolate.cloudera.org:4242/")
DIST_TEST_HOME = os.environ.get('DIST_TEST_HOME',
                                os.path.expanduser("~/dist_test"))

# The number of times that flaky tests will be retried.
# Our non-distributed implementation sets a number of _attempts_, not a number
# of retries, so we have to subtract 1.
FLAKY_TEST_RETRIES = int(os.environ.get('KUDU_FLAKY_TEST_ATTEMPTS', 1)) - 1

PATH_TO_REPO = "../"

TEST_COMMAND_RE = re.compile('Test command: (.+)$')
LDD_RE = re.compile(r'^\s+.+? => (\S+) \(0x.+\)')

DEPS_FOR_ALL = \
    ["build-support/stacktrace_addr2line.pl",
     "build-support/run-test.sh",
     "build-support/run_dist_test.py",
     "build-support/tsan-suppressions.txt",
     "build-support/lsan-suppressions.txt",

     # The LLVM symbolizer is necessary for suppressions to work
     "thirdparty/installed/bin/llvm-symbolizer",

     # Tests that use the external minicluster require these.
     # TODO: declare these dependencies per-test.
     "build/latest/bin/kudu-tserver",
     "build/latest/bin/kudu-master",
     "build/latest/bin/kudu-ts-cli",

     # parser-test requires these data files.
     # TODO: again, we should do this with some per-test metadata file.
     # TODO: these are broken now that we separate source and build trees.
     #".../example-deletes.txt",
     #".../example-tweets.txt",

     # Tests that require tooling require these.
     "build/latest/bin/kudu-admin",
     ]


class StagingDir(object):
  @staticmethod
  def new():
    dir = rel_to_abs("build/isolate")
    if os.path.isdir(dir):
      shutil.rmtree(dir)
    os.makedirs(dir)
    return StagingDir(dir)

  def __init__(self, dir):
    self.dir = dir

  def archive_dump_path(self):
    return os.path.join(self.dir, "dump.json")

  def gen_json_paths(self):
    return glob.glob(os.path.join(self.dir, "*.gen.json"))

  def tasks_json_path(self):
    return os.path.join(self.dir, "tasks.json")


def rel_to_abs(rel_path):
  dirname, _ = os.path.split(os.path.abspath(__file__))
  abs = os.path.abspath(os.path.join(dirname, PATH_TO_REPO, rel_path))
  if rel_path.endswith('/') and not abs.endswith('/'):
    abs += '/'
  return abs


def abs_to_rel(abs_path, staging):
  rel = os.path.relpath(abs_path, staging.dir)
  if abs_path.endswith('/') and not rel.endswith('/'):
    rel += '/'
  return rel


def get_test_commandlines():
  ctest_bin = os.path.join(rel_to_abs("thirdparty/installed/bin/ctest"))
  p = subprocess.Popen([ctest_bin, "-V", "-N", "-LE", "no_dist_test"], stdout=subprocess.PIPE)
  out, err = p.communicate()
  if p.returncode != 0:
    print >>sys.stderr, "Unable to list tests with ctest"
    sys.exit(1)
  lines = out.splitlines()
  commands = []
  for l in lines:
    m = TEST_COMMAND_RE.search(l)
    if not m:
      continue
    commands.append(shlex.split(m.group(1)))
  return commands


def is_lib_blacklisted(lib):
  # These particular system libraries, we should ship to the remote nodes.
  # No need to ship things like libc, libstdcxx, etc.
  if "boost" in lib or "oauth" in lib:
    return False
  if lib.startswith("/lib") or lib.startswith("/usr"):
    return True
  return False


def is_outside_of_tree(path):
  repo_dir = rel_to_abs("./")
  rel = os.path.relpath(path, repo_dir)
  return rel.startswith("../")

def copy_system_library(lib):
  """
  For most system libraries, we expect them to be installed on the test
  machines. However, a couple are shipped from the submitter machine
  to the cluster by putting them in a special directory inside the
  isolated build tree.

  This function copies such libraries into that directory.
  """
  sys_lib_dir = rel_to_abs("build/dist-test-system-libs")
  if not os.path.exists(sys_lib_dir):
    os.makedirs(sys_lib_dir)
  dst = os.path.join(sys_lib_dir, os.path.basename(lib))
  if not os.path.exists(dst):
    logging.info("Copying system library %s to %s...", lib, dst)
    shutil.copy2(rel_to_abs(lib), dst)
  return dst


def ldd_deps(exe):
  """
  Runs 'ldd' on the provided 'exe' path, returning a list of
  any libraries it depends on. Blacklisted libraries are
  removed from this list.

  If the provided 'exe' is not a binary executable, returns
  an empty list.
  """
  if exe.endswith(".sh"):
    return []
  p = subprocess.Popen(["ldd", exe], stdout=subprocess.PIPE)
  out, err = p.communicate()
  if p.returncode != 0:
    print >>sys.stderr, "failed to run ldd on ", exe
    return []
  ret = []
  for l in out.splitlines():
    m = LDD_RE.match(l)
    if not m:
      continue
    lib = m.group(1)
    if is_lib_blacklisted(lib):
      continue
    path = m.group(1)
    ret.append(m.group(1))

    # ldd will often point to symlinks. We need to upload the symlink
    # as well as whatever it's pointing to, recursively.
    while os.path.islink(path):
      path = os.path.join(os.path.dirname(path), os.readlink(path))
      ret.append(path)
  return ret


def num_shards_for_test(test_name):
  if 'raft_consensus-itest' in test_name:
    return 8
  if 'cfile-test' in test_name:
    return 4
  if 'mt-tablet-test' in test_name:
    return 4
  return 1


def create_archive_input(staging, argv,
                         disable_sharding=False):
  """
  Generates .gen.json and .isolate files corresponding to the
  test command 'argv'. The outputs are placed in the specified
  staging directory.

  Some larger tests are automatically sharded into several tasks.
  If 'disable_sharding' is True, this behavior will be suppressed.
  """
  if not argv[0].endswith('run-test.sh') or len(argv) < 2:
    print >>sys.stderr, "Unable to handle test: ", argv
    return
  test_name = os.path.basename(argv[1])
  abs_test_exe = os.path.realpath(argv[1])
  rel_test_exe = abs_to_rel(abs_test_exe, staging)
  argv[1] = rel_test_exe
  files = []
  files.append(rel_test_exe)
  deps = ldd_deps(abs_test_exe)
  for d in DEPS_FOR_ALL:
    d = os.path.realpath(rel_to_abs(d))
    if os.path.isdir(d):
      d += "/"
    deps.append(d)
  for d in deps:
    # System libraries will end up being relative paths out
    # of the build tree. We need to copy those into the build
    # tree somewhere.
    if is_outside_of_tree(d):
      d = copy_system_library(d)
    files.append(abs_to_rel(d, staging))

  if disable_sharding:
    num_shards = 1
  else:
    num_shards = num_shards_for_test(test_name)
  for shard in xrange(0, num_shards):
    out_archive = os.path.join(staging.dir, '%s.%d.gen.json' % (test_name, shard))
    out_isolate = os.path.join(staging.dir, '%s.%d.isolate' % (test_name, shard))

    command = ['../../build-support/run_dist_test.py',
               '-e', 'GTEST_SHARD_INDEX=%d' % shard,
               '-e', 'GTEST_TOTAL_SHARDS=%d' % num_shards,
               '-e', 'KUDU_TEST_TIMEOUT=%d' % (TEST_TIMEOUT_SECS - 30),
               '-e', 'KUDU_ALLOW_SLOW_TESTS=%s' % os.environ.get('KUDU_ALLOW_SLOW_TESTS', 1),
               '-e', 'KUDU_COMPRESS_TEST_OUTPUT=%s' % \
                      os.environ.get('KUDU_COMPRESS_TEST_OUTPUT', 0)]
    command.append('--')
    command += argv[1:]

    archive_json = dict(args=["-i", out_isolate,
                              "-s", out_isolate + "d"],
                        dir=rel_to_abs("."),
                        name='%s.%d/%d' % (test_name, shard + 1, num_shards),
                        version=1)
    isolate_dict = dict(variables=dict(command=command,
                                       files=files))
    with open(out_archive, "w") as f:
      json.dump(archive_json, f)
    with open(out_isolate, "w") as f:
      pprint.pprint(isolate_dict, f)


def create_task_json(staging,
                     replicate_tasks=1,
                     flaky_test_set=set()):
  """
  Create a task JSON file suitable for submitting to the distributed
  test execution service.

  If 'replicate_tasks' is higher than one, each .isolate file will be
  submitted multiple times. This can be useful for looping tests.
  """
  tasks = []
  with file(staging.archive_dump_path(), "r") as isolate_dump:
    inmap = json.load(isolate_dump)

  # Some versions of 'isolate batcharchive' directly list the items in
  # the dumped JSON. Others list it in an 'items' dictionary.
  items = inmap.get('items', inmap)
  for k, v in items.iteritems():
    # The key is 'foo-test.<shard>'. So, chop off the last component
    # to get the test name
    test_name = ".".join(k.split(".")[:-1])
    max_retries = 0
    if test_name in flaky_test_set:
      max_retries = FLAKY_TEST_RETRIES

    tasks += [{"isolate_hash": str(v),
               "description": str(k),
               "artifact_archive_globs": ARTIFACT_ARCHIVE_GLOBS,
               "timeout": TEST_TIMEOUT_SECS + 30,
               "max_retries": max_retries
               }] * replicate_tasks

  outmap = {"tasks": tasks}

  with file(staging.tasks_json_path(), "wt") as f:
    json.dump(outmap, f)


def run_isolate(staging):
  """
  Runs 'isolate batcharchive' to archive all of the .gen.json files in
  the provided staging directory.

  Throws an exception if the call fails.
  """
  isolate_path = "isolate"
  try:
    subprocess.check_call([isolate_path,
                           'batcharchive',
                           '-isolate-server=' + ISOLATE_SERVER,
                           '-dump-json=' + staging.archive_dump_path(),
                           '--'] + staging.gen_json_paths())
  except:
    print >>sys.stderr, "Failed to run", isolate_path
    raise

def submit_tasks(staging, options):
  """
  Runs the distributed testing tool to submit the tasks in the
  provided staging directory.

  This requires that the tasks JSON file has already been generated
  by 'create_task_json()'.
  """
  if not os.path.exists(DIST_TEST_HOME):
    print >>sys.stderr, "Cannot find dist_test tools at path %s " \
        "Set the DIST_TEST_HOME environment variable to the path to the dist_test directory. " \
        % DIST_TEST_HOME,
    raise OSError("Cannot find path to dist_test tools")
  client_py_path = os.path.join(DIST_TEST_HOME, "client.py")
  try:
    cmd = [client_py_path, "submit"]
    if options.no_wait:
      cmd.append('--no-wait')
    cmd.append(staging.tasks_json_path())
    subprocess.check_call(cmd)
  except:
    print >>sys.stderr, "Failed to run", client_py_path
    raise

def get_flakies():
  path = os.getenv('KUDU_FLAKY_TEST_LIST')
  if not path:
    return set()
  return set(l.strip() for l in file(path))

def run_all_tests(parser, options):
  """
  Gets all of the test command lines from 'ctest', isolates them,
  creates a task list, and submits the tasks to the testing service.
  """
  commands = get_test_commandlines()
  staging = StagingDir.new()
  for command in commands:
    create_archive_input(staging, command,
        disable_sharding=options.disable_sharding)

  run_isolate(staging)
  create_task_json(staging, flaky_test_set=get_flakies())
  submit_tasks(staging, options)

def add_run_all_subparser(subparsers):
  p = subparsers.add_parser('run-all', help='Run all of the dist-test-enabled tests')
  p.set_defaults(func=run_all_tests)

def loop_test(parser, options):
  """
  Runs many instances of a user-provided test case on the testing service.
  """
  if options.num_instances < 1:
    parser.error("--num-instances must be >= 1")
  command = ["run-test.sh", options.cmd] + options.args
  staging = StagingDir.new()
  create_archive_input(staging, command,
                       disable_sharding=options.disable_sharding)
  run_isolate(staging)
  create_task_json(staging, options.num_instances)
  submit_tasks(staging, options)

def add_loop_test_subparser(subparsers):
  p = subparsers.add_parser('loop', help='Run many instances of the same test',
      epilog="if passing arguments to the test, you may want to use a '--' " +
             "argument before <test-path>. e.g: loop -- foo-test --gtest_opt=123")
  p.add_argument("--num-instances", "-n", dest="num_instances", type=int,
                 help="number of test instances to start", metavar="NUM",
                 default=100)
  p.add_argument("cmd", help="test binary")
  p.add_argument("args", nargs=argparse.REMAINDER, help="test arguments")
  p.set_defaults(func=loop_test)


def main(argv):
  logging.basicConfig(level=logging.INFO)
  p = argparse.ArgumentParser()
  p.add_argument("--disable-sharding", dest="disable_sharding", action="store_true",
                 help="Disable automatic sharding of tests", default=False)
  p.add_argument("--no-wait", dest="no_wait", action="store_true",
                 help="Return without waiting for the job to complete", default=False)
  sp = p.add_subparsers()
  add_loop_test_subparser(sp)
  add_run_all_subparser(sp)
  args = p.parse_args(argv)
  args.func(p, args)


if __name__ == "__main__":
  main(sys.argv[1:])
