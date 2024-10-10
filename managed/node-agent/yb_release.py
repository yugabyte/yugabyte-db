#!/usr/bin/env python3
# Copyright (c) YugaByte, Inc.

import argparse
from distutils.command.build import build
import json
import logging
import os
import shutil
import subprocess
import tarfile

from ybops.utils import init_env, log_message, get_default_release_version, get_release_file
from ybops.common.exceptions import YBOpsRuntimeError

"""This script packages the node agent binaries for all supported platforms.
"""

# Supported platforms for node-agent.
NODE_AGENT_PLATFORMS = set(["linux/amd64", "linux/arm64"])


def filter_function(version, filename):
    exclude_folders = ["{}/devops/venv".format(version)]
    for exclude_folder in exclude_folders:
        if filename.startswith(exclude_folder):
            logging.info("Skipping {}".format(filename))
            return False
    return True


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
parser.add_argument('--pre_release', help='Generate pre-release packages.', action='store_true')
parser.add_argument('--include_pex', help='Include pex env for node agent',
                    action='store_true')
# Because we don't know whats the best way to send this argument
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
    # Rebuild minimal pex for node agent.
    if args.pre_release:
        devops_home = os.getenv('DEVOPS_HOME')
        if devops_home is None:
            raise YBOpsRuntimeError("DEVOPS_HOME not found")

        if args.include_pex:
            logging.info("Rebuilding pex for node agent.")
            build_pex_script = os.path.join(devops_home, "bin", "build_ansible_pex.sh")
            subprocess.check_call([build_pex_script, '--force'])
    for platform in NODE_AGENT_PLATFORMS:
        parts = platform.split("/")
        packaged_file = os.path.join(args.source_dir, "build",
                                     "node_agent-{}-{}-{}.tar.gz"
                                     .format(version, parts[0], parts[1]))
        # Devops cannot parse names separated by dashes.
        release_file = get_release_file(args.source_dir,
                                        "node_agent", os_type=parts[0], arch_type=parts[1])
        shutil.copyfile(packaged_file, release_file)
        if args.pre_release:
            # Pre-release is for local testing only.
            release_file = packaged_file
            if args.include_pex:
                repackaged_file = os.path.join(args.source_dir, "build",
                                               "node_agent-{}-{}-{}-repackaged.tar.gz"
                                               .format(version, parts[0], parts[1]))
                with tarfile.open(repackaged_file, "w|gz") as repackaged_tarfile:
                    with tarfile.open(release_file, "r|gz") as tarfile:
                        for member in tarfile:
                            repackaged_tarfile.addfile(member, tarfile.extractfile(member))

                    repackaged_tarfile.add(devops_home, arcname="{}/devops".format(version),
                                           filter=lambda x: x if filter_function(version, x.name)
                                           else None)

                    thirdparty_folder = "/opt/third-party/"
                    for file in os.listdir(thirdparty_folder):
                        # Skip Packaging alertmanager with NodeAgent.
                        # TODO: Make a list of packages that are actually needed.
                        if "alertmanager" in file:
                            continue
                        filepath = os.path.join(thirdparty_folder, file)
                        if os.path.isfile(filepath):
                            repackaged_tarfile.add(filepath,
                                                   "{}/thirdparty/{}".format(version, file))

                shutil.move(repackaged_file, packaged_file)

        logging.info("Copying file {} to {}".format(release_file, args.destination))
        shutil.copy(release_file, args.destination)
    # Delete ansible only pex.
    if args.pre_release and args.include_pex:
        logging.info("Deleting ansible only pex after packaging.")
        pex_path = os.path.join(devops_home, "pex", "pexEnv")
        if os.path.isdir(pex_path):
            shutil.rmtree(pex_path)

except (OSError, shutil.SameFileError) as e:
    log_message(logging.ERROR, e)
    raise e
