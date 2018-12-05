#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import shutil
import argparse
import glob
from subprocess import check_output, CalledProcessError
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
# IMPORTANT: DO NOT REMOVE THIS FLAG. REQUIRED BY INTERNAL INFRA.
parser.add_argument('--force', help='Force no user input', action="store_true")
args = parser.parse_args()

output = None

try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))
    output = check_output(["sbt", "clean"])
    shutil.rmtree(os.path.join(script_dir, "ui", "node_modules"), ignore_errors=True)
    log_message(logging.INFO, "Copy Map Tiles from S3 Repo")
    mapDownloadPath = os.path.join(script_dir, 'ui', 'public', 'map')
    if not os.path.exists(mapDownloadPath):
        os.makedirs(mapDownloadPath)
    output = check_output(['aws', 's3', 'sync', 's3://no-such-url', mapDownloadPath])
    log_message(logging.INFO, "Building/Packaging UI code")
    output = check_output(["npm", "install"], cwd=os.path.join(script_dir, 'ui'))
    output = check_output(["npm", "run", "build"], cwd=os.path.join(script_dir, 'ui'))
    log_message(logging.INFO, "Kick off SBT universal packaging")
    output = check_output(["sbt", "universal:packageZipTarball"])
    log_message(logging.INFO, "Get a release file name based on the current commit sha")
    release_file = get_release_file(script_dir, "yugaware")
    packaged_files = glob.glob(os.path.join(script_dir, "target", "universal", "yugaware*.tgz"))
    if len(packaged_files) == 0:
        raise YBOpsRuntimeError("Yugaware packaging failed")
    log_message(logging.INFO, "Rename the release file to have current commit sha")
    shutil.copyfile(packaged_files[0], release_file)
    shutil.rmtree(mapDownloadPath, ignore_errors=True)
    if args.destination:
        if not os.path.exists(args.destination):
            raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
        shutil.copy(release_file, args.destination)

except (CalledProcessError, OSError, RuntimeError, TypeError, NameError) as e:
    log_message(logging.ERROR, e)
    log_message(logging.ERROR, output)
