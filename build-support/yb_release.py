#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import argparse
from ybops.utils import init_env, log_message
from ybops.release_manager import ReleaseManager

parser = argparse.ArgumentParser()
parser.add_argument('--publish', action="store_true")
args = parser.parse_args()

init_env(logging.INFO)
repository_root = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
build_dir = os.path.join("build", "latest")

release_type = 'debug'
# We need to find out what the symlink path looks like.
if 'release' in os.path.basename(os.path.realpath(build_dir)):
    release_type = 'release'

try:
    release_manager = ReleaseManager({"repository": repository_root,
                                      "name": "yb-server",
                                      "type": release_type})
    log_message(logging.INFO, "Generating release")
    release_manager.generate_release()
    if args.publish:
        log_message(logging.INFO, "Publishing release")
        release_manager.publish_release()
except RuntimeError as e:
    log_message(logging.ERROR, e)
