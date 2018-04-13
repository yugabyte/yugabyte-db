#!/usr/bin/env python2
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
import platform
import shutil
import subprocess
import tempfile
import uuid
import yaml

from yb.library_packager import LibraryPackager, add_common_arguments
from yb.mac_library_packager import MacLibraryPackager, add_common_arguments
from yb.release_util import ReleaseUtil, check_for_local_changes
from yb.common_util import init_env, get_build_type_from_build_root, set_thirdparty_dir

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
                        help='Whether or not we should build a package. This defaults to '
                             'false if --build_target is specified, true otherwise.')
    parser.add_argument('--destination', help='Copy release to Destination folder.')
    parser.add_argument('--force', help='Skip prompts', action='store_true')
    parser.add_argument('--edition', help='Which edition the code is built as.',
                        default=None,
                        choices=RELEASE_EDITION_ALLOWED_VALUES)
    parser.add_argument('--commit', help='Custom specify a git commit.')
    parser.add_argument('--skip_build', help='Skip building the code', action='store_true')
    parser.add_argument('--build_target',
                        help='Target directory to put the YugaByte distribution into. This can '
                             'be used for debugging this script without having to build the '
                             'tarball. If specified, this directory must either not exist or be '
                             'empty.')
    parser.add_argument('--keep_tmp_dir', action='store_true',
                        help='Keep the temporary directory (for debugging).')
    parser.add_argument('--save_release_path_to_file',
                        help='Save the newly built release path to a file with this name. '
                             'This allows to post-process / upload the newly generated release '
                             'in an enclosing script.')
    parser.add_argument('--java_only', action="store_true",
                        help="Only build the java part of the code. Does not generate an archive!")
    add_common_arguments(parser)
    args = parser.parse_args()

    init_env(args.verbose)

    if not args.build_target and not args.build_archive and not args.java_only:
        logging.info("Implying --build_archive (build package) because --build_target "
                     "(a custom directory to put YB distribution files into) is not specified.")
        args.build_archive = True

    if not args.build_archive and args.save_release_path_to_file:
        raise RuntimeError('--save_release_path_to_file does not make sense without '
                           '--build_archive')

    build_root = args.build_root
    if build_root and not args.edition:
        build_root_basename = os.path.basename(build_root)
        if '-community-' in build_root_basename or build_root_basename.endswith('community'):
            logging.info("Setting edition to Community based on build root")
            args.edition = RELEASE_EDITION_COMMUNITY

    if not args.edition:
        # Here we are not detecting edition based on the existence of the enterprise source
        # directory.
        args.edition = RELEASE_EDITION_ENTERPRISE

    build_edition = "enterprise" if args.edition == RELEASE_EDITION_ENTERPRISE else "community"

    build_type = args.build_type

    tmp_dir = os.path.join(YB_SRC_ROOT, "build", "yb_release_tmp_{}".format(str(uuid.uuid4())))
    try:
        os.mkdir(tmp_dir)
    except OSError as e:
        logging.error("Could not create directory at '{}'".format(tmp_dir))
        raise e
    if not args.keep_tmp_dir:
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

    logging.info("Building YugaByte DB {} Edition: '{}' build{}".format(
        build_edition.capitalize(), build_type, " Java only" if args.java_only else ""))

    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')
    build_cmd_list = [
        "./yb_build.sh",
        "--with-assembly",
        "--write-build-descriptor", build_desc_path,
        "--edition", build_edition,
        build_type
    ]

    if build_root:
        # This will force yb_build.sh to use this build directory, and detect build type,
        # compiler type, edition, etc. based on that.
        build_cmd_list += ["--build-root", build_root]

    if args.java_only:
        build_cmd_list += ["--java-only"]
    else:
        build_cmd_list += [
            # This will build the exact set of targets that are needed for the release.
            "packaged_targets"
        ]
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

    thirdparty_dir = build_desc["thirdparty_dir"]
    if thirdparty_dir != os.environ.get("YB_THIRDPARTY_DIR", thirdparty_dir):
        raise RuntimeError(
            "Mismatch between env YB_THIRDPARTY_DIR: '{}' and build desc thirdparty_dir: '{}'"
            .format(os.environ["YB_THIRDPARTY_DIR"], thirdparty_dir))
    # Set the var for in-memory access to it across the other python files.
    set_thirdparty_dir(thirdparty_dir)

    # This points to the release manifest within the release_manager, and we are modifying that
    # directly.
    release_util = ReleaseUtil(YB_SRC_ROOT, build_type, args.edition, build_target, args.force,
                               args.commit)
    release_util.rewrite_manifest(build_root)

    if not args.java_only:
        system = platform.system().lower()
        if system == "linux":
            library_packager = LibraryPackager(
                build_dir=build_root,
                seed_executable_patterns=release_util.get_binary_path(),
                dest_dir=yb_distribution_dir,
                verbose_mode=args.verbose)
        elif system == "darwin":
            library_packager = MacLibraryPackager(
                    build_dir=build_root,
                    seed_executable_patterns=release_util.get_binary_path(),
                    dest_dir=yb_distribution_dir,
                    verbose_mode=args.verbose)
        else:
            raise RuntimeError("System {} not supported".format(system))
        library_packager.package_binaries()

    release_util.update_manifest(yb_distribution_dir)

    logging.info("Generating release distribution")

    if os.path.exists(build_target) and os.listdir(build_target):
        if args.java_only:
            logging.info("Directory '{}' exists and is not empty, but we are using --java_only")
        else:
            raise RuntimeError("Directory '{}' exists and is non-empty".format(build_target))
    release_util.create_distribution(build_target, "java" if args.java_only else None)

    if args.build_archive:
        release_file = os.path.realpath(release_util.generate_release())
        if args.destination:
            if not os.path.exists(args.destination):
                raise RuntimeError("Destination {} not a directory.".format(args.destination))
            shutil.copy(release_file, args.destination)
        logging.info("Generated a package at '{}'".format(release_file))

        if args.save_release_path_to_file:
            with open(args.save_release_path_to_file, 'w') as release_path_file:
                release_path_file.write(release_file)

            logging.info("Saved package path to '{}'".format(
                args.save_release_path_to_file))


if __name__ == '__main__':
    main()
