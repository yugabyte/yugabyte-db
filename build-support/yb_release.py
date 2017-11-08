#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

import argparse
import atexit
import glob
import imp
import json
import logging
import os
import shutil
import sys
import tempfile
import yaml

from xml.dom import minidom
from subprocess import call
from ybops.common.exceptions import YBOpsRuntimeError
from ybops.release_manager import ReleaseManager
from ybops.utils import init_env, log_message, RELEASE_EDITION_ALLOWED_VALUES, \
    RELEASE_EDITION_ENTERPRISE, RELEASE_EDITION_COMMUNITY
from yb.library_packager import LibraryPackager, add_common_arguments

YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', help='Build type (debug/release)',
                        default="release",
                        dest='build_type')
    parser.add_argument('--build-args',
                        help='Additional arguments to pass to the build script',
                        dest='build_args',
                        default='')
    parser.add_argument('--publish', action='store_true', help='Publish release to S3.')
    parser.add_argument('--destination', help='Copy release to Destination folder.')
    parser.add_argument('--force', help='Skip prompts', action='store_true')
    parser.add_argument('--edition', help='Which edition the code is built as.',
                        default=RELEASE_EDITION_ENTERPRISE,
                        choices=RELEASE_EDITION_ALLOWED_VALUES)
    parser.add_argument('--skip-build', help='Skip building the code', action='store_true')
    parser.add_argument(
            '--extracted-destination',
            help='Instead of building a tarball, put the distribution in the given'
                 'directory. This is equivalent to building and extracting the tarball, but '
                 'much faster.')
    add_common_arguments(parser)
    args = parser.parse_args()

    init_env(logging.DEBUG if args.verbose else logging.INFO)
    log_message(logging.INFO, "Building YugaByte code: '{}' build".format(args.build_type))

    tmp_dir = tempfile.mkdtemp(suffix=os.path.basename(__file__))
    atexit.register(lambda: shutil.rmtree(tmp_dir))

    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')
    yb_distribution_dir = os.path.join(tmp_dir, 'yb_distribution')

    os.chdir(YB_SRC_ROOT)

    # Parse Java project version out of java/pom.xml.
    java_project_version = minidom.parse('java/pom.xml').getElementsByTagName(
            'version')[0].firstChild.nodeValue
    log_message(logging.INFO, "Java project version from pom.xml: {}".format(java_project_version))

    build_edition = "enterprise" if args.edition == RELEASE_EDITION_ENTERPRISE else "community"
    build_cmd_list = [
        "./yb_build.sh", args.build_type, "--with-assembly",
        "--write-build-descriptor", build_desc_path,
        "--edition", build_edition,
        # This will build the exact set of targets that are needed for the release.
        "packaged_targets",
        args.build_args
        ]

    build_cmd_line = " ".join(build_cmd_list).strip()
    log_message(logging.INFO, "Build command line: {}".format(build_cmd_line))
    if call(build_cmd_line, shell=True) != 0:
        raise RuntimeError('Build failed')
    if not os.path.exists(build_desc_path):
        raise IOError("The build script failed to generate build descriptor file at '{}'".format(
                build_desc_path))
    with open(build_desc_path) as build_desc_file:
        build_desc = yaml.load(build_desc_file)

    log_message(logging.INFO, "Build descriptor: {}".format(build_desc))

    if args.extracted_destination and args.publish:
        raise RuntimeError('--extracted-destination and --publish are incompatible')

    build_dir = build_desc['build_root']
    release_manager = ReleaseManager({"repository": YB_SRC_ROOT,
                                      "name": "yugabyte",
                                      "type": args.build_type,
                                      "edition": args.edition,
                                      "force_yes": args.force})

    # This points to the release manifest within the release_manager, and we are modifying that
    # directly.
    release_manifest = release_manager.release_manifest
    for key, value_list in release_manifest.iteritems():
        for i in xrange(len(value_list)):
            new_value = value_list[i].replace('${project.version}', java_project_version)
            if new_value != value_list[i]:
                log_message(logging.INFO,
                            "Substituting Java project version in '{}' -> '{}'".format(
                                value_list[i], new_value))
                value_list[i] = new_value

    library_packager = LibraryPackager(
            build_dir=build_dir,
            seed_executable_patterns=release_manifest['bin'],
            dest_dir=yb_distribution_dir,
            verbose_mode=args.verbose,
            include_system_libs=not args.no_system_libs,
            include_licenses=args.include_licenses)
    library_packager.package_binaries()

    for release_subdir in ['bin']:
        if release_subdir in release_manifest:
            del release_manifest[release_subdir]
    for root, dirs, files in os.walk(yb_distribution_dir):
        release_manifest.setdefault(os.path.relpath(root, yb_distribution_dir), []).extend(
                [os.path.join(root, f) for f in files])

    if args.verbose:
        log_message(logging.INFO,
                    "Effective release manifest:\n" +
                    json.dumps(release_manifest, indent=2, sort_keys=True))

    log_message(logging.INFO, "Generating release")

    if args.extracted_destination:
        extracted_dest_dir = args.extracted_destination
        if os.path.exists(extracted_dest_dir) and os.listdir(extracted_dest_dir):
            raise RuntimeError("Directory '{}' exists and is non-empty".format(extracted_dest_dir))
        for dir_from_manifest in release_manifest:
            current_dest_dir = os.path.join(extracted_dest_dir, dir_from_manifest)
            if os.path.exists(current_dest_dir):
                if args.verbose:
                    logging.info("Directory '{}' already exists".format(current_dest_dir))
            else:
                os.makedirs(current_dest_dir)

            for elem in release_manifest[dir_from_manifest]:
                if not elem.startswith('/'):
                    elem = os.path.join(YB_SRC_ROOT, elem)
                files = glob.glob(elem)
                for file_path in files:
                    if os.path.islink(file_path):
                        link_path = os.path.join(current_dest_dir, os.path.basename(file_path))
                        link_target = os.readlink(file_path)
                        if args.verbose:
                            log_message(logging.INFO,
                                        "Creating symlink {} -> {}".format(link_path, link_target))
                        os.symlink(link_target, link_path)
                    elif os.path.isdir(file_path):
                        current_dest_dir = os.path.join(current_dest_dir,
                                                        os.path.basename(file_path))
                        if args.verbose:
                            log_message(logging.INFO,
                                        "Copying directory {} to {}".format(file_path,
                                                                            current_dest_dir))
                        shutil.copytree(file_path, current_dest_dir)
                    else:
                        current_dest_dir = os.path.join(extracted_dest_dir, dir_from_manifest)
                        if args.verbose:
                            log_message(
                                    logging.INFO,
                                    "Copying file {} to directory {}".format(file_path,
                                                                             current_dest_dir))
                        shutil.copy(file_path, current_dest_dir)
        log_message(logging.INFO,
                    "Creating an non-tar distribution at '{}'".format(extracted_dest_dir))
    else:
        # We've already updated the release manifest inside release_manager with the auto-generated
        # set of executables and libraries to package.
        release_file = release_manager.generate_release()

    if args.publish:
        log_message(logging.INFO, "Publishing release")
        release_manager.publish_release()
    elif args.destination:
        if not os.path.exists(args.destination):
            raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
        shutil.copy(release_file, args.destination)


if __name__ == '__main__':
    main()
