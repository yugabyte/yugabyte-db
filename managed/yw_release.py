#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import shutil
import sys
from subprocess import check_output, CalledProcessError
from ybops.utils import init_env, log_message, get_release_file, publish_release, generate_checksum

"""This script is basically does bunch of different thing
  - Builds the React API and generates production files (js, css, etc)
  - Run sbt packaging command to package yugaware along with the react files
  - Rename the package file to have the commit sha in it
  - Generate checksum for the package
  - Publish the package to s3
"""
try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))

    log_message(logging.INFO, "Building/Packaging UI code")
    check_output(["npm", "run", "build"], cwd=os.path.join(script_dir, 'ui'))

    log_message(logging.INFO, "Kick off SBT universal packaging")
    check_output(["sbt", "universal:packageZipTarball"])

    log_message(logging.INFO, "Get a release file name based on the current commit sha")
    release_file = get_release_file(script_dir, 'yugaware')
    packaged_file = os.path.join(script_dir, 'target', 'universal', 'yugaware-1.0-SNAPSHOT.tgz')

    log_message(logging.INFO, "Rename the release file to have current commit sha")
    shutil.copyfile(packaged_file, release_file)

    log_message(logging.INFO, "Publish the release to S3")
    generate_checksum(release_file)
    publish_release(script_dir, release_file)

except (CalledProcessError, OSError, RuntimeError, TypeError, NameError) as e:
    log_message(logging.ERROR, e)
