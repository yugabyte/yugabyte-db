#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import argparse
import glob
import logging
import os
import shutil
import tarfile
from subprocess import Popen, PIPE, CalledProcessError
from ybops.utils import init_env, log_message, get_release_file
from ybops.common.exceptions import YBOpsRuntimeError

"""This script is basically builds and packages yugaware application.
  - Builds the React API and generates production files (js, css, etc)
  - Run sbt packaging command to package yugaware along with the react files
  - Rename the package file to have the commit sha in it
  - Move the package file to the required destination
  - A higher level user, such as itest, will upload all release packages to s3 release bucket

"""

parser = argparse.ArgumentParser()
parser.add_argument('--destination', help='Copy release to Destination folder.')
parser.add_argument('--tag', help='Release tag name')
parser.add_argument(
    '--unarchived', action='store_true',
    help='Untar release in "target/universal" directory or in --destination if specified.')
# IMPORTANT: DO NOT REMOVE THIS FLAG. REQUIRED BY INTERNAL INFRA.
parser.add_argument('--force', help='Force no user input', action="store_true")
args = parser.parse_args()

try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))

    _, err = Popen(["sbt", "clean", "-batch"], stderr=PIPE).communicate()
    if err:
        raise RuntimeError(err)
    log_message(logging.INFO, "Kick off SBT universal packaging")
    # Ignore any error output from packaging as npm is expected to have some warnings.
    os.system("df -h; sbt universal:packageZipTarball -batch")
    log_message(logging.INFO, "Finished running SBT universal packaging.")

    packaged_files = glob.glob(os.path.join(script_dir, "target", "universal", "yugaware*.tgz"))
    if args.unarchived:
        package_tar = packaged_files[0]
        tar = tarfile.open(package_tar)
        tar.extractall(args.destination) if args.destination else tar.extractall()
    else:
        log_message(logging.INFO, "Get a release file name based on the current commit sha")
        release_file = get_release_file(script_dir, "yugaware")
        if len(packaged_files) == 0:
            raise YBOpsRuntimeError("Yugaware packaging failed")
        log_message(logging.INFO, "Rename the release file to have current commit sha")
        shutil.copyfile(packaged_files[0], release_file)
        if args.destination:
            if not os.path.exists(args.destination):
                raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
            shutil.copy(release_file, args.destination)
except CalledProcessError as e:
    log_message(logging.error, e)
    log_message(logging.error, e.output)
except (OSError, RuntimeError, TypeError, NameError) as e:
    log_message(logging.ERROR, e)
    raise e
