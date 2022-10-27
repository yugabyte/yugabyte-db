#!/usr/bin/env python3
# Copyright (c) YugaByte, Inc.

import argparse
from distutils.command.build import build
import json
import logging
import os
import shutil
import subprocess

from ybops.utils import init_env, log_message, get_default_release_version, get_release_file
from ybops.common.exceptions import YBOpsRuntimeError

"""This script packages the node agent binaries for all supported platforms.
"""

# Supported platforms for node-agent.
NODE_AGENT_PLATFORMS = set(["darwin/amd64", "linux/amd64", "linux/arm64"])


def get_release_version(source_dir):
    version_metadata = os.path.join(source_dir, "version_metadata.json")
    with open(version_metadata, 'r') as file:
        data = file.read()
    obj = json.loads(data)
    version = obj.get('version_number')
    if version is None:
        log_message(logging.WARN, "Version number is not found in version_metadata.json.")
        return get_default_release_version()
    build_num = obj.get('build_number')
    if build_num is None:
        log_message(logging.WARN, "Build number is not found in version_metadata.json.")
        return get_default_release_version()
    version_format = "{}-b{}" if build_num.isdigit() else "{}-{}"
    return version_format.format(version, build_num)


parser = argparse.ArgumentParser()
parser.add_argument('--source_dir', help='Source code directory.', required=True)
parser.add_argument('--destination', help='Copy release to Destination directory.', required=True)
args = parser.parse_args()

try:
    init_env(logging.INFO)
    if not os.path.exists(args.destination):
        raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))

    version = get_release_version(args.source_dir)
    build_script = os.path.join(args.source_dir, "build.sh")
    process_env = os.environ.copy()
    process_env["NODE_AGENT_PLATFORMS"] = ' '.join(NODE_AGENT_PLATFORMS)
    subprocess.check_call([build_script, 'clean', 'build', 'package', version], env=process_env)
    for platform in NODE_AGENT_PLATFORMS:
        parts = platform.split("/")
        packaged_file = os.path.join(args.source_dir, "build",
                                     "node_agent-{}-{}-{}.tar.gz"
                                     .format(version, parts[0], parts[1]))
        # Devops cannot parse names separated by dashes.
        release_file = get_release_file(args.source_dir,
                                        "node_agent", os_type=parts[0], arch_type=parts[1])
        shutil.copyfile(packaged_file, release_file)
        logging.info("Copying file {} to {}".format(release_file, args.destination))
        shutil.copy(release_file, args.destination)

except (OSError, shutil.SameFileError) as e:
    log_message(logging.ERROR, e)
    raise e
