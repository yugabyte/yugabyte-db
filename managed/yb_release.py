#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import shutil
import sys
import argparse

from subprocess import check_output, CalledProcessError
from ybops.utils import init_env, log_message, get_release_file, publish_release, \
    generate_checksum, latest_release, download_release, docker_push_to_replicated
from ybops.utils.release import get_package_info, S3_RELEASE_BUCKET, \
    _extract_components_from_package_name
from ybops.common.exceptions import YBOpsRuntimeError

"""This script is basically builds and packages yugaware application.
  - Builds the React API and generates production files (js, css, etc)
  - Run sbt packaging command to package yugaware along with the react files
  If we want to publish a docker image, then we just generate and publish
  or else, we would do the following to generate a release file.
  - Rename the package file to have the commit sha in it
  - Generate checksum for the package
  - Publish the package to s3

"""

parser = argparse.ArgumentParser()
release_types = ["docker", "file"]
parser.add_argument('--type', action='store', choices=release_types,
                   default="file", help='Provide a release type')
parser.add_argument('--publish', action='store_true',
                    help='Publish release to S3.')
parser.add_argument('--destination', help='Copy release to Destination folder.')
parser.add_argument('--tag', help='Release tag name')
args = parser.parse_args()

output = None
try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))
    output = check_output(["sbt", "clean"])

    if args.type == "docker":
        # TODO, make these params, so we can specify which version of devops
        # and yugabyte software we want to be bundled, also be able to specify
        # local builds
        if not args.tag:
            raise YBOpsRuntimeError("--tag is required for docker release.")

        packages_folder = os.path.join(script_dir, "target", "docker", "stage", "packages")
        package_infos = None
        try:
            log_message(logging.INFO, "Download packages based on the release tag")
            package_infos = get_package_info(args.tag)
            yugaware_commit_hash = None
            for package_info in package_infos:
                package_name = package_info.get("package")
                repo, commit, version = _extract_components_from_package_name(package_name, True)
                if repo == "yugaware":
                    yugaware_commit_hash = commit
                    continue
                elif repo == "yugabyte":
                    download_folder = os.path.join(packages_folder, repo, version)
                else:
                    download_folder = os.path.join(packages_folder, repo)

                if not os.path.exists(download_folder):
                    os.makedirs(download_folder)

                download_release(args.tag, package_name, download_folder, S3_RELEASE_BUCKET)

            if not yugaware_commit_hash:
                raise YBOpsRuntimeError("Unable to fetch yugaware commit hash.")
            output = check_output(["git", "checkout", yugaware_commit_hash])
            log_message(logging.INFO, "Package and publish yugaware docker image locally")
            output = check_output(["sbt", "docker:publishLocal"])
            log_message(logging.INFO, "Package and publish yugaware-ui docker image locally")
            output = check_output(["docker", "build", "-t", "yugaware-ui", "ui"])

            if args.publish:
                log_message(logging.INFO, "Tag Yugaware and Yugaware UI with replicated urls")
                docker_push_to_replicated("yugaware", "1.0-SNAPSHOT", args.tag)
                docker_push_to_replicated("yugaware-ui", "latest", args.tag)

        except YBOpsRuntimeError as ye:
            log_message(logging.ERROR, ye)
            log_message(logging.ERROR, "Invalid release tag provided.")
        finally:
            output = check_output(["git", "checkout", "master"])
            shutil.rmtree(packages_folder)

    else:
        log_message(logging.INFO, "Building/Packaging UI code")
        shutil.rmtree(os.path.join(script_dir, "ui", "node_modules"), ignore_errors=True)
        output = check_output(["npm", "install"], cwd=os.path.join(script_dir, 'ui'))
        output = check_output(["npm", "run", "build"], cwd=os.path.join(script_dir, 'ui'))

        log_message(logging.INFO, "Kick off SBT universal packaging")
        output = check_output(["sbt", "universal:packageZipTarball"])

        log_message(logging.INFO, "Get a release file name based on the current commit sha")
        release_file = get_release_file(script_dir, 'yugaware')
        packaged_file = os.path.join(script_dir, 'target', 'universal', 'yugaware-1.0-SNAPSHOT.tgz')

        log_message(logging.INFO, "Rename the release file to have current commit sha")
        shutil.copyfile(packaged_file, release_file)

        if args.publish:
            log_message(logging.INFO, "Publish the release to S3")
            generate_checksum(release_file)
            publish_release(script_dir, release_file)
        elif args.destination:
            if not os.path.exists(args.destination):
                raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
            shutil.copy(release_file, args.destination)

except (CalledProcessError, OSError, RuntimeError, TypeError, NameError) as e:
    log_message(logging.ERROR, e)
    log_message(logging.ERROR, output)
