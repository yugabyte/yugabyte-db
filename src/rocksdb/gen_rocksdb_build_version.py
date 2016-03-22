#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

import logging
import argparse
import os
import re
import sha
import subprocess
import sys
import datetime

def main():
  logging.basicConfig(
    level=logging.INFO,
    format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

  parser = argparse.ArgumentParser(
      usage="usage: %(prog)s <options>")
  parser.add_argument(
    "--git-sha",
    help="Git SHA passed in from the Makefile",
    type=unicode,
    dest="git_sha",
    metavar="GIT_SHA",
    required=True)

  parser.add_argument(
    "--output-path",
    help="Output file to generate",
    type=unicode,
    dest="output_path",
    metavar="VERSION",
    required=True)

  args = parser.parse_args()

  minimize_recompilation = 'YB_MINIMIZE_RECOMPILATION' in os.environ

  git_sha = args.git_sha
  if git_sha == '' and '/.CLion' in os.path.abspath(__file__) and not minimize_recompilation:
    logging.warn("This appears to be a CLion-initiaed build and --git-sha argument is empty. " +
                 "Acting as if YB_MINIMIZE_RECOMPILATION is set.")
    minimize_recompilation = True

  if minimize_recompilation:
    git_sha = '0' * 40
  elif len(git_sha) != 40:
    logging.error('Git SHA is expected to be 40 characters, found %d: %s' %
      (len(git_sha), git_sha))
    return 1

  output_path = args.output_path
  date_str = datetime.datetime.now().strftime("%Y-%m-%d")

  output_dir = os.path.dirname(output_path)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  log_file_path = os.path.join(output_dir,
    os.path.splitext(os.path.basename(__file__))[0] + '.log')

  file_log_handler = logging.FileHandler(log_file_path)
  file_log_handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s: %(message)s"))
  logging.getLogger('').addHandler(file_log_handler)

  compile_date_str = '__DATE__'
  if minimize_recompilation:
    logging.info(
      ('Removing git_sha and date from the new contents of "%s" as required by ' +
       'YB_MINIMIZE_RECOMPILATION to reduce unnecessary rebuilds.') % output_path)
    date_str = '0000-00-00'
    compile_date_str = date_str

  new_contents = (
"""
#include "build_version.h"
const char* rocksdb_build_git_sha = "rocksdb_build_git_sha:%s";
const char* rocksdb_build_git_date = "rocksdb_build_git_date:%s";
const char* rocksdb_build_compile_date = "%s";
""" % (git_sha, date_str, compile_date_str)).strip() + "\n"

  # Do not overwrite the file if it already contains the same code we are going to write.
  # We do not want to update the modified timestamp on this file unnecessarily, as this may trigger
  # additional recompilation.

  should_write = False
  output_exists = os.path.exists(output_path)
  old_contents = open(output_path).read() if output_exists else ''
  if not output_exists:
    logging.info("File '%s' does not exist, will create" % output_path)
    should_write = True
  elif old_contents.strip() != new_contents.strip():
    logging.info("File '%s' has different contents from what what is needed, will overwrite" %
      output_path)
    logging.info("Old contents:\n" + old_contents.strip())
    logging.info("New contents:\n" + new_contents.strip())
    should_write = True
  else:
    logging.info("Not rewriting '%s' (no changes)" % output_path)

  if should_write:
    with file(output_path, "w") as f:
      print >>f, new_contents

  return 0

if __name__ == "__main__":
  sys.exit(main())

