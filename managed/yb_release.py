#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import os
import logging
import shutil
import sys
import argparse
import tarfile
import random
import string
import requests
import json
import glob
from subprocess import check_output, CalledProcessError
from ybops.utils import init_env, log_message, get_release_file, publish_release, \
    generate_checksum, latest_release, download_release, docker_push_to_registry
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
release_types = ["docker", "file", "replicated"]
parser.add_argument('--type', action='store', choices=release_types,
                   default="file", help='Provide a release type')
parser.add_argument('--publish', action='store_true',
                    help='Publish release to S3.')
parser.add_argument('--destination', help='Copy release to Destination folder.')
parser.add_argument('--tag', help='Release tag name')
parser.add_argument('--packages', nargs='+',  help='Comma separated release packages' +
                    'ex. devops-sha.tar.gz, yugaware-sha.tar.gz, yugabyte-sha.tar.gz ')
parser.add_argument('--release_notes', help='Release notes', nargs='+', default="")
args = parser.parse_args()

output = None
SECRET_CHOICE = string.ascii_lowercase + string.digits + '!@#$%^&*(-_=+)'
REQUIRED_PACKAGES = ('devops', 'yugaware', 'yugabyte')
REPLICATED_VENDOR_API = 'https://api.replicated.com/vendor/v1/app/'

try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))

    if args.type == "docker":
        packages = args.packages
        if not packages and not args.tag:
            raise YBOpsRuntimeError("--tag or --packages is required for docker release.")
        elif packages:
            packages = [package for package in args.packages
                        if os.path.basename(package).startswith(REQUIRED_PACKAGES)]
            if len(packages) < 3:
                raise YBOpsRuntimeError("Required packages {} not specified".format(REQUIRED_PACKAGES))
        elif args.tag:
            log_message(logging.INFO, "Download packages based on the release tag")
            packages = [p.get("package") for p in get_package_info(args.tag)]

        packages_folder = os.path.join(script_dir, "target", "docker", "packages")

        try:
            yugabyte_package = None
            yugaware_package = None
            for package in packages:
                package_name = os.path.basename(package)
                repo, commit, version = _extract_components_from_package_name(package_name, True)
                download_folder = os.path.join(packages_folder, repo)
                if repo == "yugaware":
                    yugaware_package = os.path.join(download_folder, package_name)
                elif repo == "yugabyte":
                    download_folder = os.path.join(packages_folder, repo, version)
                    yugabyte_package = os.path.join(download_folder, package_name)

                if not os.path.exists(download_folder):
                    os.makedirs(download_folder)

                if args.packages:
                    log_message(logging.INFO, "Copy local package {}".format(package_name))
                    shutil.copy(package, os.path.join(download_folder, package_name))
                else:
                    log_message(logging.INFO, "Download package {} from s3".format(package_name))
                    download_release(args.tag, package_name, download_folder, S3_RELEASE_BUCKET)

            # Get the YB Load tester tar alone
            yugabyte_tarfile = tarfile.open(yugabyte_package)
            log_message(logging.INFO, "Get yb-sample-apps jar from yugabyte tarfile")
            for archive_file in yugabyte_tarfile.getmembers():
                if "yb-sample-apps" in archive_file.name:
                    yugabyte_tarfile.extract(archive_file, packages_folder)
                    log_message(logging.INFO, archive_file.name)

            log_message(logging.INFO, "Package and publish YugaWare docker image locally")
            output = check_output(["docker", "build", "-t", "yugaware", "."])

            if args.publish and args.tag:
                log_message(logging.INFO, "Publish YugaWare docker image to Quay.io")
                docker_push_to_registry("yugaware", "latest", args.tag)
                docker_push_to_registry("yugaware", "latest", "latest")
        except YBOpsRuntimeError as ye:
            log_message(logging.ERROR, ye)
            log_message(logging.ERROR, "Invalid release tag provided.")
        finally:
            shutil.rmtree(packages_folder)

    elif args.type == "replicated":
        # Validated if the tag is valid release
        _ = get_package_info(args.tag)
        log_message(logging.INFO, "Creating replicated release")
        # TODO move this to devops?
        secret_str = ''.join([random.SystemRandom().choice(SECRET_CHOICE) for i in range(64)])
        with open("replicated.yml", "r") as yaml_file:
            yaml_str = yaml_file.read()\
                .replace('YUGABYTE_RELEASE_VERSION', args.tag)\
                .replace('YUGAWARE_APP_SECRET', secret_str)
            api_token = os.environ.get('REPLICATED_API_TOKEN')
            app_id = os.environ.get('REPLICATED_APP_ID')
            if api_token is None or app_id is None:
                raise YBOpsRuntimeError("REPLICATED_API_TOKEN and REPLICATED_APP_ID are required.")

            headers = {'authorization': api_token, 'content-type': 'application/json'}
            params = {'start': 0, 'count': 1}
            # Fetch the current releases and channel information
            response = requests.get(os.path.join(REPLICATED_VENDOR_API, app_id, 'releases', 'paged'),
                                    headers=headers, json=params)
            response.raise_for_status()
            releases = json.loads(response.text)
            current_beta_release = None
            beta_channel_id = None

            for release in releases['releases']:
                release_channels = [channel for channel in release['ActiveChannels']
                                    if channel['Name'] == "Beta"]
                print release_channels
                if len(release_channels) >= 1:
                    current_beta_release = release_channels[0]['ReleaseLabel']
                    beta_channel_id = release_channels[0]['Id']
                    break
            if beta_channel_id is None or current_beta_release is None:
                raise YBOpsRuntimeError("Unable to fetch current release version.")
            elif current_beta_release == args.tag:
                raise YBOpsRuntimeError("Release already promoted")
            # Create a new release in replicated
            params = {'name': args.tag, 'source': 'latest', 'sourcedata': 0}
            response = requests.post(os.path.join(REPLICATED_VENDOR_API, app_id, 'release'),
                                     headers=headers, json=params)
            response.raise_for_status()
            response_json = json.loads(response.text)
            release_sequence = str(response_json['Sequence'])
            headers['content-type'] = 'text/plain'
            # Update the release with new replicated yaml
            response = requests.put(os.path.join(REPLICATED_VENDOR_API, app_id, release_sequence, 'raw'),
                                    headers=headers, data=yaml_str)
            response.raise_for_status()
            headers['content-type'] = 'application/json'
            params = {'channels': [beta_channel_id], 'label': args.tag,
                      'release_notes': ' '.join(args.release_notes), 'required': False}
            # Promote the release with Beta tag
            response = requests.post(os.path.join(REPLICATED_VENDOR_API, app_id, release_sequence, 'promote'),
                                    headers=headers, json=params)
            response.raise_for_status()
    else:
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
        release_file = get_release_file(script_dir, 'yugaware')
        packaged_files = glob.glob(os.path.join(script_dir, 'target', 'universal', 'yugaware*.tgz'))
        if (len(packaged_files) == 0):
            raise YBOpsRuntimeError("Yugaware packaging failed")
        log_message(logging.INFO, "Rename the release file to have current commit sha")
        shutil.copyfile(packaged_files[0], release_file)
        shutil.rmtree(mapDownloadPath, ignore_errors=True)
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
