#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import argparse
import atexit
import logging
import os
import shutil
import sys
import tempfile
import yaml
import imp

from subprocess import call
from ybops.common.exceptions import YBOpsRuntimeError
from ybops.release_manager import ReleaseManager
from ybops.utils import init_env, log_message
from yb.library_packager import LibraryPackager


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
    parser.add_argument('--verbose', help='Show verbose output', action='store_true')
    parser.add_argument('--force', help='Skip prompts', action='store_true')
    args = parser.parse_args()

    init_env(logging.DEBUG if args.verbose else logging.INFO)
    repository_root = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    log_message(logging.INFO, "Building Yugabyte code: '{}' build".format(args.build_type))

    tmp_dir = tempfile.mkdtemp(suffix=os.path.basename(__file__))
    atexit.register(lambda: shutil.rmtree(tmp_dir))
    build_desc_path = os.path.join(tmp_dir, 'build_descriptor.yaml')
    yb_distribution_dir = os.path.join(tmp_dir, 'yb_distribution')

    os.chdir(repository_root)
    build_cmd_line = "./yb_build.sh {} --with-assembly --write-build-descriptor {} {}".format(
            args.build_type, build_desc_path, args.build_args).strip()
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
    release_manager = ReleaseManager({"repository": repository_root,
                                      "name": "yugabyte",
                                      "type": args.build_type,
                                      "force_yes": args.force})

    # This points to the release manifest within the release_manager, and we are modifying that
    # directly.
    release_manifest = release_manager.release_manifest
    library_packager = LibraryPackager(
            build_dir=build_dir,
            seed_executable_patterns=release_manifest['bin'],
            dest_dir=yb_distribution_dir)
    library_packager.package_binaries()

    for release_subdir in ['bin']:
        if release_subdir in release_manifest:
            del release_manifest[release_subdir]
    for root, dirs, files in os.walk(yb_distribution_dir):
        release_manifest.setdefault(os.path.relpath(root, yb_distribution_dir), []).extend([
                os.path.join(root, f) for f in files])

    log_message(logging.INFO, "Generating release")

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
