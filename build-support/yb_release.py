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
import logging
import os
import shutil
import subprocess
import tempfile
import yaml

from yb.library_packager import LibraryPackager, add_common_arguments
from yb.release_util import ReleaseUtil, check_for_local_changes
from yb.common_util import init_env, get_build_type_from_build_root

YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RELEASE_EDITION_ENTERPRISE = "ee"
RELEASE_EDITION_COMMUNITY = "ce"
RELEASE_EDITION_ALLOWED_VALUES = set([RELEASE_EDITION_ENTERPRISE, RELEASE_EDITION_COMMUNITY])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', help='Build type (debug/release)', dest='build_type')
    parser.add_argument('--build_root',
                        help='The root directory where the code is being built. If the build type '
                             'is specified, it needs to be consistent with the build root.')
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
        logging.info("Implying --build_archive (build release archive) because --build_target "
                     "(a custom directory to put YB distribution files into) is not specified.")
        args.build_archive = True

    build_edition = "enterprise" if args.edition == RELEASE_EDITION_ENTERPRISE else "community"
    build_root = args.build_root
    build_type = args.build_type

    tmp_dir = tempfile.mkdtemp(suffix=os.path.basename(__file__))
    atexit.register(lambda: shutil.rmtree(tmp_dir))

    yb_distribution_dir = os.path.join(tmp_dir, 'yb_distribution')

    os.chdir(YB_SRC_ROOT)

    if not args.force:
        check_for_local_changes()

    # This is not a "target" in terms of Make / CMake, but a target directory.
    build_target = args.build_target
    if build_target is None:
        # Use a temporary directory.
        build_target = os.path.join(tmp_dir, 'tmp_yb_distribution')

    if build_root:
        build_type_from_build_root = get_build_type_from_build_root(build_root)
        if build_type:
            if build_type != build_type_from_build_root:
                raise RuntimeError(
                    ("Specified build type ('{}') is inconsistent with the specified build "
                     "root ('{}')").format(build_type, build_root))
        else:
            build_type = build_type_from_build_root

    if not build_type:
        build_type = 'release'

    logging.info("Building YugaByte DB {} Edition: '{}' build".format(
        build_edition.capitalize(), build_type))

    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')
    build_cmd_list = [
        "./yb_build.sh",
        "--with-assembly",
        "--write-build-descriptor", build_desc_path,
        "--edition", build_edition,
        # This will build the exact set of targets that are needed for the release.
        "packaged_targets",
        build_type
    ]
    if build_root:
        # This will force yb_build.sh to use this build directory, and detect build type,
        # compiler type, edition, etc. based on that.
        build_cmd_list += ["--build-root", build_root]
    if args.skip_build:
        build_cmd_list += ["--skip-build"]
    if args.build_args:
        build_cmd_list += args.build_args.strip().split()

    build_cmd_line = " ".join(build_cmd_list).strip()
    logging.info("Build command line: {}".format(build_cmd_line))
    subprocess.check_call(build_cmd_list)

    if not os.path.exists(build_desc_path):
        raise IOError("The build script failed to generate build descriptor file at '{}'".format(
                build_desc_path))

    with open(build_desc_path) as build_desc_file:
        build_desc = yaml.load(build_desc_file)

    logging.info("Build descriptor: {}".format(build_desc))

    build_root_from_build_desc = build_desc['build_root']
    if not build_root:
        build_root = build_root_from_build_desc
    build_root = os.path.realpath(build_root)
    if build_root != os.path.realpath(build_root_from_build_desc):
        raise RuntimeError(
            "Build root from the build descriptor file (see above) is inconsistent with that "
            "specified on the command line ('{}')".format(build_root))

    # This points to the release manifest within the release_manager, and we are modifying that
    # directly.
    release_util = ReleaseUtil(YB_SRC_ROOT, build_type, args.edition, build_target, args.force)
    release_util.rewrite_manifest()

    library_packager = LibraryPackager(
            build_dir=build_root,
            seed_executable_patterns=release_util.get_binary_path(),
            dest_dir=yb_distribution_dir,
            verbose_mode=args.verbose,
            include_licenses=args.include_licenses)
    library_packager.package_binaries()

    release_util.update_manifest(yb_distribution_dir)

    logging.info("Generating release distribution")

    if os.path.exists(build_target) and os.listdir(build_target):
        raise RuntimeError("Directory '{}' exists and is non-empty".format(build_target))
    release_util.create_distribution(build_target)

    if args.build_archive:
        release_file = release_util.generate_release()
        if args.destination:
            if not os.path.exists(args.destination):
                raise RuntimeError("Destination {} not a directory.".format(args.destination))
            shutil.copy(release_file, args.destination)
        logging.info("Generated a release archive at '{}'".format(release_file))


if __name__ == '__main__':
    main()
