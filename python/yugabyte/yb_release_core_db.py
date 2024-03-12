#!/usr/bin/env python3

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
import logging
import os
import platform
import shutil
import shlex
import subprocess
import traceback
import ruamel.yaml

from yugabyte.library_packager import LibraryPackager, add_common_arguments
from yugabyte.mac_library_packager import MacLibraryPackager, add_common_arguments
from yugabyte.release_util import ReleaseUtil, check_for_local_changes
from yugabyte.common_util import (
    init_logging,
    get_build_type_from_build_root,
    set_thirdparty_dir,
    YB_SRC_ROOT,
    create_temp_dir,
    shlex_join,
)
from yugabyte.linuxbrew import set_build_root
from yugabyte.optional_components import (
    OptionalComponents,
    add_optional_component_arguments,
    optional_components_from_args,
)

from typing import Optional, List, Union, Dict, Any, cast


def parse_args() -> argparse.Namespace:
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
    parser.add_argument('--commit', help='Specifies a custom git commit to use in archive name.')
    parser.add_argument('--skip_build', help='Skip building the code', action='store_true')
    parser.add_argument('--build_target',
                        help='Target directory to put the YugabyteDB distribution into. This can '
                             'be used for debugging this script without having to build the '
                             'tarball. If specified, this directory must either not exist or be '
                             'empty.')
    parser.add_argument('--keep_tmp_dir',
                        action='store_true',
                        help='Keep the temporary directory (for debugging).')
    parser.add_argument('--save_release_path_to_file',
                        help='Save the newly built release path to a file with this name. '
                             'This allows to post-process / upload the newly generated release '
                             'in an enclosing script.')
    parser.add_argument('--yw', action='store_true', help='Package YugaWare too.')
    parser.add_argument('--no_reinitdb', action='store_true',
                        help='Do not re-create the initial sys catalog snapshot. Useful when '
                             'debugging the release process.')
    parser.add_argument('--package_name',
                        default='yugabyte',
                        help='Name of package to release ("yugabyte" or "yugabyte-client").')
    add_common_arguments(parser)
    add_optional_component_arguments(parser)
    args = parser.parse_args()

    # Init logging immediately before calling any logging methods. Otherwise, basicConfig will have
    # no effect and we will miss log messages.
    init_logging(verbose=args.verbose)

    if not args.build_target and not args.build_archive:
        logging.info("Implying --build_archive (build package) because --build_target "
                     "(a custom directory to put YB distribution files into) is not specified.")
        args.build_archive = True

    if not args.build_archive and args.save_release_path_to_file:
        raise ValueError('--save_release_path_to_file does not make sense without '
                         '--build_archive')

    return args


def prepare_yb_build_args(
        args: argparse.Namespace,
        build_root: Optional[str],
        build_type: str,
        build_descriptor_path: str,
        optional_components: OptionalComponents,
        is_final_build: bool) -> List[str]:
    """
    Set up the command line arguments for the yb_build.sh script.

    :param args: The command line arguments to the yb_release script.
    :param build_root: The build root directory, if specified.
    :param build_type: The build type, e.g. "release" or "debug".
    :param build_descriptor_path: The path to the build descriptor file to be written.
    :param optional_components: The optional components to be built.
    :param is_final_build: whether this is the final build step (after building individual targets).
    """
    yb_build_args: List[str] = [
        "./yb_build.sh",
        "--write-build-descriptor",
        build_descriptor_path,
        build_type
    ]

    if args.force:
        yb_build_args.append("--force")

    if build_root:
        # This will force yb_build.sh to use this build directory, and detect build type,
        # compiler type, etc. based on that.
        yb_build_args.extend(["--build-root", build_root])

    if not args.yw:
        yb_build_args.append("--skip-java")

    if args.skip_build:
        yb_build_args.append("--skip-build")

    if is_final_build:
        yb_build_args.append(
            # This will build the exact set of targets that are needed for the release.
            "packaged_targets"
        )
        # Keep optional_components as is.
    else:
        # If we are building individual targets one by one, we disable all the optional components.
        # We will build them with the final build step.
        optional_components = OptionalComponents.all_disabled()

    if args.build_args:
        yb_build_args += shlex.split(args.build_args.strip())

    yb_build_args.extend(optional_components.get_yb_build_args())

    return yb_build_args


def perform_build_and_return_descriptor(
        args: argparse.Namespace,
        build_root: Optional[str],
        build_type: str,
        tmp_dir: str,
        optional_components: OptionalComponents) -> str:
    """
    Performs YugabyteDB build and returns the build descriptor file path. This works by first
    building certain specific targets individually to work around potential dependency issues
    during concurrent builds (these issues may or may not be present as of 2023), and then building
    all targets required for creating the release package.

    :param args: The command line arguments to the yb_release script.
    :param build_root: The build root directory.
    :param build_type: The build type, e.g. "release" or "debug".
    :param tmp_dir: The temporary directory to use for the build. The build descriptor file will be
                    created in this directory.
    :param optional_components: The optional components to be built.
    """

    logging.info("Building YugabyteDB {} build".format(build_type))

    # "Build descriptor" is a file produced by yb_build.sh that contains information about the
    # build type, build root, architecture, CMake build type, compiler type, third-party directory.
    build_descriptor_path = os.path.join(tmp_dir, 'build_descriptor.yaml')

    yb_build_args_full = prepare_yb_build_args(
        args, build_root, build_type, build_descriptor_path, optional_components,
        is_final_build=True)
    yb_build_args_prefix = prepare_yb_build_args(
        args, build_root, build_type, build_descriptor_path, optional_components,
        is_final_build=False)

    # ---------------------------------------------------------------------------------------------
    # Perform the build
    # ---------------------------------------------------------------------------------------------

    logging.info("Build command line: " + shlex_join(yb_build_args_full))

    if not args.skip_build:
        # TODO: figure out the dependency issues in our CMake build instead.
        # TODO: move this into yb_build.sh itself.
        # yugabyted-ui build only needs to run once, so avoid wasting time building it in these
        # preliminary builds.
        for preliminary_target in ['protoc-gen-insertions', 'bfql_codegen']:
            preliminary_step_cmd_list = yb_build_args_prefix + ['--target', preliminary_target]

            # Skipping Java in these "preliminary" builds whether or not we are building YugaWare.
            # We will still build YBClient Java code needed for YugaWare as part of the final
            # build step below.
            preliminary_step_cmd_list += ["--skip-java"]

            logging.info(
                    "Running a preliminary yb_build.sh step to build target %s: %s",
                    preliminary_target,
                    shlex_join(preliminary_step_cmd_list))
            subprocess.check_call(preliminary_step_cmd_list)

    # We still need to call this even when --skip-build is specified to generate the "build
    # descriptor" YAML file.
    final_build_cmd_list = yb_build_args_full + ([] if args.no_reinitdb else ['reinitdb'])
    logging.info("Running final yb_build.sh step: %s", shlex_join(final_build_cmd_list))
    subprocess.check_call(final_build_cmd_list)

    return build_descriptor_path


def read_build_descriptor(
        build_descriptor_path: str,
        build_root: Optional[str]) -> Dict[str, Any]:
    """
    Reads the build descriptor file and returns its contents as a dictionary.

    Also validates that the build root in the build descriptor file is consistent with the build
    root passed to this function.

    :param build_descriptor_path: The path to the build descriptor file.
    :param build_root: The build root directory specified on the command line of the yb_release
                       script.
    """
    if not os.path.exists(build_descriptor_path):
        raise IOError(
            "The build script failed to generate build descriptor file at '{}'".format(
                build_descriptor_path))

    yaml = ruamel.yaml.YAML()
    with open(build_descriptor_path) as build_desc_file:
        build_desc = yaml.load(build_desc_file)

    logging.info("Build descriptor: {}".format(build_desc))

    build_root_from_build_desc = build_desc['build_root']
    if not build_root_from_build_desc:
        raise ValueError(
            "Build descriptor file %s does not specify the build root directory. "
            "Build descriptor contents: %s" % (build_descriptor_path, build_desc))
    if not build_root:
        build_root = build_root_from_build_desc
    assert build_root is not None
    build_root = os.path.realpath(build_root)
    if build_root != os.path.realpath(build_root_from_build_desc):
        raise ValueError(
            "Build root from the build descriptor file (see above) is inconsistent with that "
            "specified on the command line ('{}')".format(build_root))
    return build_desc


def validate_thirdparty_dir(thirdparty_dir: str) -> None:
    thirdparty_dir_from_env = os.environ.get("YB_THIRDPARTY_DIR", thirdparty_dir)
    if (thirdparty_dir != thirdparty_dir_from_env and
            thirdparty_dir_from_env != os.path.join(YB_SRC_ROOT, 'thirdparty')):
        raise ValueError(
            "Mismatch between non-default valueo of env YB_THIRDPARTY_DIR: '{}' and build desc "
            "thirdparty_dir: '{}'".format(thirdparty_dir_from_env, thirdparty_dir))

    # Set the var for in-memory access to it across the other python files.
    set_thirdparty_dir(thirdparty_dir)


def create_library_packager(
        build_root: str,
        seed_executable_patterns: List[str],
        dest_dir: str,
        verbose_mode: bool) -> Union[LibraryPackager, MacLibraryPackager]:
    """
    Creates a library packager instance based on the current operating system. The library packager
    is responsible for discovering the full set of shared librares required by the given set of
    "seed" executables, and copying them to the destination directory in well-organized directory
    structure.
    """

    # TODO: these two classes should have a common base class (interface).
    library_packager: Union[LibraryPackager, MacLibraryPackager]

    system = platform.system().lower()
    if system == "linux":
        library_packager = LibraryPackager(
            build_dir=build_root,
            seed_executable_patterns=seed_executable_patterns,
            dest_dir=dest_dir,
            verbose_mode=verbose_mode)
    elif system == "darwin":
        library_packager = MacLibraryPackager(
            build_dir=build_root,
            seed_executable_patterns=seed_executable_patterns,
            dest_dir=dest_dir,
            verbose_mode=verbose_mode)
    else:
        raise RuntimeError(
            "System {} not supported: cannot create a library packager instance".format(system))

    return library_packager


def build_archive(release_util: ReleaseUtil,
                  args: argparse.Namespace) -> None:
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


def main() -> None:
    args = parse_args()

    build_root: Optional[str] = args.build_root
    build_type: Optional[str] = args.build_type

    tmp_dir = create_temp_dir(keep_tmp_dir=args.keep_tmp_dir)

    library_packager_dest_dir = os.path.join(tmp_dir, 'library_packager_output')

    os.chdir(YB_SRC_ROOT)

    if not args.force:
        check_for_local_changes()

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

    optional_components = optional_components_from_args(args)
    logging.info("Optional components: {}".format(optional_components))

    build_descriptor_path = perform_build_and_return_descriptor(
        args=args,
        build_root=build_root,
        build_type=build_type,
        tmp_dir=tmp_dir,
        optional_components=optional_components
    )

    build_desc = read_build_descriptor(build_descriptor_path, build_root)
    build_root = cast(str, build_desc['build_root'])
    assert isinstance(build_root, str), f"Invalid build root: {build_root}"
    set_build_root(build_root)

    thirdparty_dir = build_desc["thirdparty_dir"]
    validate_thirdparty_dir(thirdparty_dir)

    # This is not a "target" in terms of Make / CMake, but a target directory.
    build_target_dir = args.build_target
    if build_target_dir is None:
        # Use a temporary directory.
        build_target_dir = os.path.join(tmp_dir, 'final_yb_distribution')

    release_util = ReleaseUtil(
        build_type=build_type,
        distribution_path=build_target_dir,
        force=args.force,
        commit=args.commit,
        build_root=build_root,
        package_name=args.package_name,
        optional_components=optional_components)

    library_packager = create_library_packager(
        build_root=build_root,
        seed_executable_patterns=release_util.get_seed_executable_patterns(),
        dest_dir=library_packager_dest_dir,
        verbose_mode=args.verbose)

    library_packager.package_binaries()

    release_util.update_manifest(library_packager_dest_dir)

    logging.info("Generating release distribution")

    if os.path.exists(build_target_dir) and os.listdir(build_target_dir):
        raise RuntimeError("Directory '{}' exists and is non-empty".format(build_target_dir))
    release_util.create_distribution(build_target_dir)

    # This will set rpath for executables and libraries when using Linuxbrew.
    library_packager.postprocess_distribution(build_target_dir)

    # ---------------------------------------------------------------------------------------------
    # Invoke YugaWare (YugabyteDB Anywhere) packaging
    # ---------------------------------------------------------------------------------------------

    if args.yw:
        managed_dir = os.path.join(YB_SRC_ROOT, "managed")
        yw_dir = os.path.join(build_target_dir, "ui")
        if not os.path.exists(yw_dir):
            os.makedirs(yw_dir)
        package_yw_cmd = [
            os.path.join(managed_dir, "yb_release"),
            "--destination", yw_dir, "--unarchived"
        ]
        logging.info(
            "Creating YugaWare package with command '{}'".format(" ".join(package_yw_cmd)))
        try:
            subprocess.check_output(package_yw_cmd, cwd=managed_dir)
        except subprocess.CalledProcessError as e:
            logging.error("Failed to build YugaWare package:\n%s", traceback.format_exc())
            logging.error("Output from YugaWare build:\n%s", e.output.decode('utf-8'))
            raise
        except OSError as e:
            logging.error("Failed to build YugaWare package: {}".format(e))
            raise

    # ---------------------------------------------------------------------------------------------
    # Create a tar.gz (we almost always do this, can be skipped in debug mode)
    # ---------------------------------------------------------------------------------------------

    if args.build_archive:
        build_archive(release_util=release_util, args=args)


if __name__ == '__main__':
    main()
