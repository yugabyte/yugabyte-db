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
from datetime import datetime
import logging
import os
import shutil
import tempfile
import yaml
import sys

from subprocess import call
from yb.library_packager import LibraryPackager, add_common_arguments
from yb.release_util import ReleaseUtil
from yb.common_util import init_env, log_message

YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RELEASE_EDITION_ENTERPRISE = "ee"
RELEASE_EDITION_COMMUNITY = "ce"
RELEASE_EDITION_ALLOWED_VALUES = set([RELEASE_EDITION_ENTERPRISE, RELEASE_EDITION_COMMUNITY])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', help='Build type (debug/release)',
                        default="release",
                        dest='build_type')
    parser.add_argument('--build_args', default='',
                        help='Additional arguments to pass to the build script')
    parser.add_argument('--build_archive', action='store_true',
                        help='Whether or not we should build a release archive. This defaults to '
                             'false if --build_target is specified, true otherwise.')
    parser.add_argument('--destination', help='Copy release to Destination folder.')
    parser.add_argument('--force', help='Skip prompts', action='store_true')
    parser.add_argument('--edition', help='Which edition the code is built as.',
                        default=RELEASE_EDITION_ENTERPRISE,
                        choices=RELEASE_EDITION_ALLOWED_VALUES)
    parser.add_argument('--skip_build', help='Skip building the code', action='store_true')
    parser.add_argument('--build_target',
                        help='Target directory to put the YugaByte distribution into. This can '
                             'be used for debugging this script without having to build the '
                             'tarball. If specified, this directory must either not exist or be '
                             'empty.')
    add_common_arguments(parser)
    args = parser.parse_args()

    init_env(args.verbose)

    if not args.build_target and not args.build_archive:
        log_message(logging.INFO,
                    "Implying --build_archive because --build_target is not specified.")
        args.build_archive = True

    log_message(logging.INFO, "Building YugaByte code: '{}' build".format(args.build_type))

    tmp_dir = tempfile.mkdtemp(suffix=os.path.basename(__file__))
    atexit.register(lambda: shutil.rmtree(tmp_dir))

    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')
    yb_distribution_dir = os.path.join(tmp_dir, 'yb_distribution')

    os.chdir(YB_SRC_ROOT)

    build_edition = "enterprise" if args.edition == RELEASE_EDITION_ENTERPRISE else "community"
    build_cmd_list = [
        "./yb_build.sh", args.build_type, "--with-assembly",
        "--write-build-descriptor", build_desc_path,
        "--edition", build_edition,
        # This will build the exact set of targets that are needed for the release.
        "packaged_targets",
        args.build_args
    ]
    if args.skip_build:
        build_cmd_list.append("--skip-build")

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

    build_dir = build_desc['build_root']
    build_target = args.build_target
    if build_target is None:
        # Use a temporary directory.
        build_target = os.path.join(tmp_dir, 'tmp_yb_distribution')

    # This points to the release manifest within the release_manager, and we are modifying that
    # directly.
    release_util = ReleaseUtil(YB_SRC_ROOT, args.build_type, args.edition, build_target, args.force)
    release_util.rewrite_manifest()

    library_packager = LibraryPackager(
            build_dir=build_dir,
            seed_executable_patterns=release_util.get_binary_path(),
            dest_dir=yb_distribution_dir,
            verbose_mode=args.verbose,
            include_licenses=args.include_licenses)
    library_packager.package_binaries()

    release_util.update_manifest(yb_distribution_dir)

    log_message(logging.INFO, "Generating release distribution")

    if os.path.exists(build_target) and os.listdir(build_target):
        raise RuntimeError("Directory '{}' exists and is non-empty".format(build_target))
    release_util.create_distribution(build_target)

    if args.build_archive:
        release_file = release_util.generate_release()
        if args.destination:
            if not os.path.exists(args.destination):
                raise RuntimeError("Destination {} not a directory.".format(args.destination))
            shutil.copy(release_file, args.destination)
        log_message(logging.INFO, "Generated a release archive at '{}'".format(release_file))


if __name__ == '__main__':
    main()
