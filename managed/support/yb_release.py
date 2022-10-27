#!/usr/bin/env python3
# Copyright (c) YugaByte, Inc.

import argparse
import logging
import os
import shutil

from ybops.utils import init_env, log_message, get_release_file
from ybops.common.exceptions import YBOpsRuntimeError

"""This script packages Anywhere's support packages according to release info.
  - Rename the package file to have the commit sha in it
  - Move the package file to the required destination
  - A higher level user, such as itest, will upload all release packages to s3 release bucket
"""

parser = argparse.ArgumentParser()
parser.add_argument('--destination', help='Copy release to Destination folder.')
parser.add_argument('--package', help='Package to rename and copy to Destination')
args = parser.parse_args()

try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))
    release_file = get_release_file(script_dir, "yugabundle_support")
    shutil.copyfile(args.package, release_file)
    if args.destination:
        if not os.path.exists(args.destination):
            raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
        shutil.copy(release_file, args.destination)
except (OSError, shutil.SameFileError) as e:
    log_message(logging.ERROR, e)
    raise e
