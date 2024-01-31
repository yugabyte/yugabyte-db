"""
Copyright (c) Yugabyte, Inc.

This module provides utilities for generating and publishing release.
"""

import glob
import json
import logging
import os
import platform
import shutil
import sys
import re
import distro  # type: ignore

from sys_detection import is_macos
from subprocess import call, check_output
from xml.dom import minidom
from yugabyte.command_util import run_program, mkdir_p, copy_deep
from yugabyte.common_util import (
    get_thirdparty_dir,
    get_compiler_type_from_build_root,
    YB_SRC_ROOT,
)
from yugabyte.optional_components import OptionalComponents

from typing import Dict, Any, Optional, cast, List

RELEASE_MANIFEST_NAME = "yb_release_manifest.json"
RELEASE_VERSION_FILE = "version.txt"
THIRDPARTY_PREFIX_RE = re.compile('^thirdparty/(.*)$')


def read_release_manifest(package_name: str) -> Dict[str, Any]:
    """
    Reads the release manifest file and returns the top-level section corresponding to the given
    package name, such as "yugabyte" or "yugabyte-client". If the given top-level section is not
    present, throws an exception.
    """
    with open(os.path.join(YB_SRC_ROOT, RELEASE_MANIFEST_NAME)) as release_manifest_file:
        release_manifest = json.load(release_manifest_file)[package_name]
    assert release_manifest is not None, \
        'Unable to read {0} file'.format(RELEASE_MANIFEST_NAME)
    return release_manifest


def filter_bin_items(
        bin_items: List[str],
        optional_components: OptionalComponents) -> List[str]:
    """
    Filter the given list of items from the "bin" directory of the release manifest based on the
    given set of enabled optional components.
    """
    # We remove some items in the 'bin' directory depending on what components are built or not.
    bin_items_to_remove = []
    if not optional_components.yugabyted_ui_enabled:
        bin_items_to_remove.append('yugabyted-ui')

    if not optional_components.odyssey_enabled:
        bin_items_to_remove.append('odyssey')

    return [
        item for item in bin_items
        if os.path.basename(item) not in bin_items_to_remove
    ]


class ReleaseUtil:
    """Packages a YugaByte package with the appropriate file naming schema."""
    release_manifest: Dict[str, Any]
    base_version: str

    build_type: str
    distribution_path: str
    force: bool
    commit: str
    build_root: str
    package_name: str

    def __init__(
            self,
            build_type: str,
            distribution_path: str,
            force: bool,
            commit: Optional[str],
            build_root: str,
            package_name: str,
            optional_components: OptionalComponents) -> None:
        """
        :param build_type: build type such as "release".
        :param distribution_path: the directory where to place the resulting archive.
        :param force: whether to skip the prompt in case there are local uncommitted changes.
        :param commit: the Git commit SHA1 to use. If not specified, it is autodetected.
        :param build_root: the build root directory corresponding to the build type.
        :param package_name: the name of the top-level section of yb_release_manifest.json, such
                             as "yugabyte" or "yugabyte-client", specifying the set of files to
                             include.
        :param optional_components: specifies which optional components to include in the release
                                    archive (e.g. yugabyted UI or Odyssey PostgreSQL connection
                                    pooler).
        """
        self.build_type = build_type
        self.build_path = os.path.join(YB_SRC_ROOT, 'build')
        self.distribution_path = distribution_path
        self.force = force
        self.commit = commit or ReleaseUtil.get_head_commit_hash()
        self.package_name = package_name

        base_version = None
        with open(os.path.join(YB_SRC_ROOT, RELEASE_VERSION_FILE)) as version_file:
            # Remove any build number in the version.txt.
            base_version = version_file.read().split("-")[0]
        assert base_version is not None, \
            'Unable to read {0} file'.format(RELEASE_VERSION_FILE)
        self.base_version = base_version

        self.release_manifest = read_release_manifest(package_name)

        self.build_root = build_root
        pom_file = os.path.join(YB_SRC_ROOT, 'java', 'pom.xml')
        version_tag = minidom.parse(pom_file).getElementsByTagName('version')[0]
        assert version_tag.firstChild is not None
        self.java_project_version = version_tag.firstChild.nodeValue  # type: ignore
        logging.info("Java project version from pom.xml: {}".format(self.java_project_version))
        self.release_manifest['bin'] = filter_bin_items(
            self.release_manifest['bin'], optional_components)
        self._rewrite_manifest()

    def get_release_manifest(self) -> Dict[str, Any]:
        return self.release_manifest

    def get_seed_executable_patterns(self) -> List[str]:
        seed_executables = cast(List[str], self.release_manifest['bin'])
        if is_macos():
            # This replicates the solution that made the macOS build work prior to D25109.
            # This may have unintended side effects of copying Postgres libraries to the "bin"
            # directory. A proper solution will be implemented in a future diff.
            seed_executables.append('$BUILD_ROOT/postgres/lib/*.so')
        return seed_executables

    def expand_value(self, old_value: str) -> str:
        """
        Expand old_value with the following changes:
        - Replace ${project.version} with the Java version from pom.xml.
        - Replace the leading "thirdparty/" with the respective YB_THIRDPARTY_DIR from the build.
        - Replace $BUILD_ROOT with the actual build_root.
        - Replace $ARCH with the machine's arch (x86_64/aarch64)
        """
        # Filter out lines that are platform specific
        if old_value.startswith('Linux-only:') or old_value.startswith('Darwin-only:'):
            if old_value.startswith('{}-only'.format(platform.system())):
                new_value = old_value.split(':', maxsplit=1)[1]
            else:
                return ''
        else:
            new_value = old_value
        # Substitution for Java.
        new_value = new_value.replace('${project.version}', self.java_project_version)
        # Substitution for thirdparty.
        thirdparty_prefix_match = THIRDPARTY_PREFIX_RE.match(new_value)
        if thirdparty_prefix_match:
            new_value = os.path.join(get_thirdparty_dir(), thirdparty_prefix_match.group(1))
        # Substitution for ARCH.
        new_value = new_value.replace("${ARCH}", platform.machine())
        # Substitution for YBCOS.  This doesn't map cleanly yet.
        # we don't provide Mac native binaries for YBC yet, so just include the linux package
        # of the appropriate arch.
        new_value = new_value.replace("${YBCOS}",
                                      {"aarch64-Linux": "el8",
                                       "x86_64-Linux": "linux",
                                       "arm64-Darwin": "el8",
                                       "x86_64-Darwin": "linux"
                                       }['-'.join([platform.machine(), platform.system()])]
                                      )
        # Substitution for BUILD_ROOT.
        new_value = new_value.replace("$BUILD_ROOT", self.build_root)
        thirdparty_intrumentation = "uninstrumented"
        new_value = new_value.replace(
            "$THIRDPARTY_BUILD_SPECIFIC_DIR",
            os.path.join(get_thirdparty_dir(), "installed", thirdparty_intrumentation))
        if new_value != old_value:
            logging.info("Substituting '{}' -> '{}' in manifest".format(
                old_value, new_value))
        return new_value

    def _rewrite_manifest(self) -> None:
        """
        Rewrite the release manifest expanding values using expand_value function.
        """
        for key, values in self.release_manifest.items():
            if isinstance(values, dict):
                for k, v in values.items():
                    values[k] = self.expand_value(v)
            else:
                for i in range(len(values)):
                    values[i] = self.expand_value(values[i])

    def repo_expand_path(self, path: str) -> str:
        """
        If path is relative treat it as a path within repo and make it absolute.
        """
        if not path.startswith('/'):
            path = os.path.join(YB_SRC_ROOT, path)
        return path

    def create_distribution(self, distribution_dir: str) -> None:
        """This method would read the release_manifest and traverse through the
        build directory and copy necessary files/symlinks into the distribution_dir
        Args:
            distribution_dir (string): Directory to create the distribution
        """
        for dir_from_manifest in self.release_manifest:
            if dir_from_manifest == '%symlinks%':
                for dst, target in self.release_manifest[dir_from_manifest].items():
                    dst = os.path.join(distribution_dir, dst)
                    logging.debug("Creating symlink {} -> {}".format(dst, target))
                    mkdir_p(os.path.dirname(dst))
                    os.symlink(target, dst)
                continue
            current_dest_dir = os.path.join(distribution_dir, dir_from_manifest)
            mkdir_p(current_dest_dir)

            for elem in self.release_manifest[dir_from_manifest]:
                elem = self.repo_expand_path(elem)
                files = glob.glob(elem)
                for file_path in files:
                    copy_deep(file_path,
                              os.path.join(current_dest_dir, os.path.basename(file_path)))
        logging.info("Created the distribution at '{}'".format(distribution_dir))

    def update_manifest(self, distribution_dir: str) -> None:
        for release_subdir in ['bin']:
            if release_subdir in self.release_manifest:
                del self.release_manifest[release_subdir]
        for root, dirs, files in os.walk(distribution_dir):
            paths = [os.path.join(root, f) for f in files]
            # We also need to include dirs which are really links to directories.
            for d in dirs:
                path = os.path.join(root, d)
                if os.path.islink(path):
                    paths.append(path)
            self.release_manifest.setdefault(os.path.relpath(root, distribution_dir), []).extend(
                paths)

        logging.debug("Effective release manifest:\n" +
                      json.dumps(self.release_manifest, indent=2, sort_keys=True))

    @staticmethod
    def get_head_commit_hash() -> str:
        return check_output(["git", "rev-parse", "HEAD"]).strip().decode('utf-8')

    def get_release_file(self) -> str:
        """
        This method does couple of checks before generating the release file name.
        - Checks if there are local uncommitted changes.
        - Checks if there are local commits which aren't merged upstream.
        - Reads the base version from the version.txt file and appends to the filename.
        Also fetches the platform the release file is being built and adds that to the file name
        along with commit hash and built type.
        Returns:
            (string): Release file path.
        """
        components: List[str] = [
            self.base_version,
            self.commit,
            self.build_type
        ]
        compiler_type = get_compiler_type_from_build_root(self.build_root)
        # Make Clang-based release packages the default, and append the compiler type for all other
        # compiler types so we can still use them with the appropriate support from the downstream
        # tooling.
        if compiler_type not in ['clang12', 'clang13']:
            components.append(compiler_type)
        release_name = "-".join(components)

        system = platform.system().lower()
        if system == "linux":
            # We recently moved from centos7 to almalinux8 as the build host for our universal
            # x86_64 linux build.  This changes the name of the release tarball we create.
            # Unfortunately, we have a lot of hard coded references to the centos package names
            # in our downsstream release code.  So here we munge the name to 'centos' to keep things
            # working while we fix downstream code.
            # TODO(jharveymsith): Remove the almalinux to centos mapping once downstream is fixed.
            if distro.id() == "centos" and distro.major_version() == "7" \
                    or distro.id() == "almalinux" and platform.machine().lower() == "x86_64":
                system = "centos"
            elif distro.id() == "ubuntu":
                system = distro.id() + distro.version()
            else:
                system = distro.id() + distro.major_version()

        release_file_name = "{}-{}-{}-{}.tar.gz".format(
            self.package_name, release_name, system, platform.machine().lower())
        return os.path.join(self.build_path, release_file_name)

    def generate_release(self) -> str:
        """
        Generates a release package and returns the path to the release file.
        """
        yugabyte_folder_prefix = "{}-{}".format(self.package_name, self.base_version)
        tmp_parent_dir = self.distribution_path + '.tmp_for_tar_gz'
        os.mkdir(tmp_parent_dir)

        # Move the distribution directory to a new location named yugabyte-<version> and archive
        # it from there so it has the right name when extracted.
        #
        # We used to do this using the --transform option to the tar command, but that has an
        # unintended side effect of corrupting library symlinks to files in the same directory.
        tmp_distribution_dir = os.path.join(tmp_parent_dir, yugabyte_folder_prefix)
        shutil.move(self.distribution_path, tmp_distribution_dir)

        def change_permissions(mode: str) -> None:
            # Logging the command as part of run_program will be enough in non-verbose mode.
            logging.debug(
                "Changing permissions recursively on directory '%s': %s", tmp_distribution_dir,
                mode)
            cmd_line = ['chmod', '-R', mode, tmp_distribution_dir]
            run_program(cmd_line, cwd=tmp_parent_dir, log_command=True)

        try:
            release_file = self.get_release_file()
            change_permissions('u+w')
            change_permissions('a+r')
            # From chmod manpage, "+X" means: set the execute/search bits if the file is a directory
            # or any of the execute/search bits are set in the original (unmodified) mode.
            change_permissions('a+X')
            logging.info("Creating a package '%s' from directory %s",
                         release_file, tmp_distribution_dir)
            run_program(['tar', 'cvzf', release_file, yugabyte_folder_prefix],
                        cwd=tmp_parent_dir)
            return release_file
        finally:
            shutil.move(tmp_distribution_dir, self.distribution_path)
            os.rmdir(tmp_parent_dir)


def check_for_local_changes() -> None:
    is_dirty = False
    if check_output(["git", "diff", "origin/master"]).strip():
        logging.error("Local changes exists. This shouldn't be an official release.")
        is_dirty = True
    elif check_output(["git", "log", "origin/master..HEAD", "--oneline"]):
        logging.error("Local commits exists. This shouldn't be an official release.")
        is_dirty = True

    if is_dirty:
        prompt_input = input("Continue [Y/n]: ").strip().lower()
        if prompt_input not in ['y', 'yes', '']:
            sys.exit(1)
