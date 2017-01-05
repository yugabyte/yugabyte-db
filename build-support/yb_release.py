#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import argparse
import shutil
from subprocess import call
from ybops.utils import init_env, log_message
from ybops.release_manager import ReleaseManager
from ybops.common.exceptions import YBOpsRuntimeError

parser = argparse.ArgumentParser()
parser.add_argument('--build', help='Build type (debug/release)', default="debug")
parser.add_argument('--publish', action='store_true', help='Publish release to S3.')
parser.add_argument('--destination', help='Copy release to Destination folder.')
args = parser.parse_args()

try:
    init_env(logging.INFO)
    repository_root = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    log_message(logging.INFO, "Building Yugabyte code: '{}' build".format(args.build))

    os.chdir(repository_root)
    call(["./yb_build.sh", args.build])

    build_dir = os.path.join("build", "latest")
    release_manager = ReleaseManager({"repository": repository_root,
                                      "name": "yb-server",
                                      "type": args.build})
    log_message(logging.INFO, "Generating release")
    release_file = release_manager.generate_release()

    if args.publish:
        log_message(logging.INFO, "Publishing release")
        release_manager.publish_release()
    elif args.destination:
        if not os.path.exists(args.destination):
            raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
        shutil.copy(release_file, args.destination)
except RuntimeError as e:
    log_message(logging.ERROR, e)
