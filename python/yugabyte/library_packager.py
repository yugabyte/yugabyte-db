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

"""
Copyright (c) YugaByte, Inc.

Finds all Linux dynamic libraries that have to be packaged with the YugabyteDB distribution tarball
by starting from a small set of executables and walking the dependency graph. Creates a
self-sufficient distribution directory.
"""


import argparse
import collections
import enum
import glob
import logging
import os
import re
import shutil
import subprocess

from collections import defaultdict
from functools import total_ordering
from queue import Queue

from yugabyte.command_util import run_program, mkdir_p, copy_deep
from yugabyte.common_util import (
    get_thirdparty_dir,
    sorted_grouped_by,
    YB_SRC_ROOT,
)
from yugabyte.rpath import set_rpath, remove_rpath
from yugabyte.file_util import clean_path_join
from yugabyte.linuxbrew import get_linuxbrew_home, using_linuxbrew, LinuxbrewHome

from typing import List, Optional, Any, Set, Tuple, Dict, cast

# A resolved shared library dependency shown by ldd.
# Example (split across two lines):
#   libmaster.so => /home/mbautin/code/yugabyte/build/debug-gcc-dynamic/lib/libmaster.so
#   (0x00007f941fa5f000)
RESOLVED_DEP_RE = re.compile(r'^\s*(\S+)\s+=>\s+(\S.*\S)\s+[(]')

SYSTEM_LIBRARY_PATH_RE = re.compile(r'^/(usr|lib|lib64)/.*')
SYSTEM_LIBRARY_PATHS = ['/usr/lib', '/usr/lib64', '/lib', '/lib64',
                        # This is used on Ubuntu
                        '/usr/lib/x86_64-linux-gnu',
                        '/lib/x86_64-linux-gnu']

HOME_DIR = os.path.expanduser('~')


ADDITIONAL_LIB_NAME_GLOBS = ['libnss_*', 'libresolv*', 'libthread_db*']

YB_SCRIPT_BIN_DIR = os.path.join(YB_SRC_ROOT, 'bin')
YB_BUILD_SUPPORT_DIR = os.path.join(YB_SRC_ROOT, 'build-support')

PATCHELF_NOT_AN_ELF_EXECUTABLE = 'not an ELF executable'

LIBRARY_PATH_RE = re.compile('^(.*[.]so)(?:$|[.].*$)')


# This is an alternative to global variables, bundling a few commonly used things.
DistributionContext = collections.namedtuple(
        'DistributionContext',
        ['build_dir',
         'build_postgres_lib_dir',
         'dest_dir',
         'verbose_mode'])


# Ensure files with these names are treated as shared libraries for the purpose of setting RPATHs.
SPECIAL_CASE_LIB_NAME_PREFIXES = (
    'liblber-',
    'libldap-',
    'libldap_r-',
)


def should_manipulate_rpath_of(file_path: str) -> bool:
    base_name = os.path.basename(file_path)
    return not os.path.islink(file_path) and (
        os.access(file_path, os.X_OK) or
        file_path.endswith('.so') or
        # Some libraries don't have the executable bit set on them. We will fix that.
        base_name.startswith(SPECIAL_CASE_LIB_NAME_PREFIXES) and '.so.' in base_name
    ) and not (
        # This directory has a few executable files that we should not manipulate.
        os.path.dirname(file_path).endswith('/pgxs/config')
    )


@total_ordering
class DependencyCategory(enum.Enum):
    # Binaries built as part of YugabyteDB but not as part of Postgres.
    YB = 'yb'

    # Binaries built as part of yugabyte-db-thirdparty
    YB_THIRDPARTY = 'yb-thirdparty'

    LINUXBREW = 'linuxbrew'

    # Libraries residing in system-wide library directories. We do not copy these.
    SYSTEM = 'system'

    POSTGRES = 'postgres'

    # Various scripts copied as is from YugabyteDB sources.
    YB_SCRIPTS = 'yb-scripts'

    def __init__(self, subdir_name: str) -> None:
        self.subdir_name = subdir_name

    def __lt__(self, other: Any) -> bool:
        if not isinstance(other, DependencyCategory):
            return False
        return self.value < cast(DependencyCategory, other).value


CATEGORIES_FOR_RPATH = [
    DependencyCategory.YB,
    DependencyCategory.YB_THIRDPARTY,
    DependencyCategory.POSTGRES
]


@total_ordering
class Dependency:
    """
    Describes a dependency of an executable or a shared library on another shared library.
    """

    # The name of the library as requested by the original executable/shared library.
    name: str

    # Target file pointed to by the dependency
    target: str

    category: Optional[DependencyCategory]
    origin: str
    context: DistributionContext

    def __init__(self, name: str, target: str, origin: str, context: DistributionContext) -> None:
        self.name = name
        self.target = target
        self.category = None
        self.context = context
        self.origin = origin

    def __hash__(self) -> int:
        return hash(self.name) ^ hash(self.target)

    def _comparison_key(self) -> Tuple[str, str]:
        return (self.name, self.target)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, Dependency):
            return False
        other_dep = cast(Dependency, other)
        return self._comparison_key() == other._comparison_key()

    def __str__(self) -> str:
        return "Dependency(name='{}', target='{}', origin='{}')".format(
                self.name, self.target, self.origin)

    def __repr__(self) -> str:
        return str(self)

    def __lt__(self, other: Any) -> bool:
        return (self.name, self.target) < (other.name, other.target)

    def get_category(self) -> DependencyCategory:
        if self.category is not None:
            return self.category

        linuxbrew_home = get_linuxbrew_home()
        if linuxbrew_home is not None and linuxbrew_home.path_is_in_linuxbrew_dir(self.target):
            self.category = DependencyCategory.LINUXBREW
        elif self.target.startswith(get_thirdparty_dir() + '/'):
            self.category = DependencyCategory.YB_THIRDPARTY
        elif self.target.startswith(self.context.build_dir + '/postgres/'):
            self.category = DependencyCategory.POSTGRES
        elif self.target.startswith(self.context.build_dir + '/'):
            self.category = DependencyCategory.YB
        elif (self.target.startswith(YB_SCRIPT_BIN_DIR + '/') or
              self.target.startswith(YB_BUILD_SUPPORT_DIR + '/')):
            self.category = DependencyCategory.YB_SCRIPTS

        if not self.category:
            for system_library_path in SYSTEM_LIBRARY_PATHS:
                if self.target.startswith(system_library_path + '/'):
                    self.category = DependencyCategory.SYSTEM
                    break

        if self.category:
            return self.category

        if linuxbrew_home:
            linuxbrew_dir_str = linuxbrew_home.get_human_readable_dirs()
        else:
            linuxbrew_dir_str = 'N/A'

        raise RuntimeError(
            ("Could not determine the category of this binary "
             "(yugabyte / yb-thirdparty / linuxbrew / system): '{}'. "
             "Does not reside in the Linuxbrew directory ({}), "
             "YB third-party directory ('{}'), "
             "YB build directory ('{}'), "
             "YB general-purpose script directory ('{}'), "
             "YB build support script directory ('{}'), "
             "and does not appear to be a system library (does not start with any of {})."
             ).format(
                self.target,
                linuxbrew_dir_str,
                get_thirdparty_dir(),
                self.context.build_dir,
                YB_SCRIPT_BIN_DIR,
                YB_BUILD_SUPPORT_DIR,
                SYSTEM_LIBRARY_PATHS))


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    """
    Add command-line arguments common between library_packager_old.py invoked as a script, and
    the yb_release.py script.
    """
    parser.add_argument('--verbose',
                        help='Enable verbose output.',
                        action='store_true')


def symlink(source: str, link_path: str) -> None:
    """
    Create a symbolic link at `link_path` pointing to the existing file or directory at `source`.
    Does nothing if the source path is absolute and the desired link already exists and points
    to the same path.
    """
    if os.path.exists(link_path):
        if not source.startswith('/') or os.path.realpath(link_path) != os.path.realpath(source):
            raise RuntimeError(
                    "Trying to create symlink '{}' -> '{}' but it already points to '{}'".format(
                        link_path, source, os.readlink(link_path)))
        # In the case source is an absolute path and the link already points at it there will be
        # no error.
    else:
        os.symlink(source, link_path)


class LibraryPackager:
    """
    A utility for starting with a set of 'seed' executables, and walking the dependency tree of
    libraries to find all libraries that need to be packaged with the product.
    """

    # Build directory of YugabyteDB. We take files from here.
    build_dir: str

    # Destination directory to install the libraries to, organized into subdirectories by type.
    dest_dir: str

    seed_executable_patterns: List[str]
    installed_dyn_linked_binaries: List[str]

    # Subdirectories of the destination directory.
    main_dest_bin_dir: str
    postgres_dest_bin_dir: str

    # Number of files in a particular directory where RPATH was set to a particular value.
    rpath_stats_by_dir: Dict[Tuple[str, str], int]

    # Number of files from which RPATH was removed, by directory.
    rpath_removal_stats_by_dir: Dict[str, int]

    def __init__(self,
                 build_dir: str,
                 seed_executable_patterns: List[str],
                 dest_dir: str,
                 verbose_mode: bool = False) -> None:
        build_dir = os.path.realpath(build_dir)
        dest_dir = os.path.realpath(dest_dir)
        if not os.path.exists(build_dir):
            raise IOError("Build directory '{}' does not exist".format(build_dir))
        self.seed_executable_patterns = seed_executable_patterns
        self.dest_dir = dest_dir
        logging.debug(
            "Traversing the dependency graph of executables/libraries, starting "
            "with seed executable patterns: {}".format(", ".join(seed_executable_patterns)))
        self.context = DistributionContext(
            dest_dir=dest_dir,
            build_dir=build_dir,
            build_postgres_lib_dir=os.path.join(build_dir, 'postgres', 'lib'),
            verbose_mode=verbose_mode)
        self.installed_dyn_linked_binaries = []
        self.main_dest_bin_dir = os.path.join(self.dest_dir, 'bin')
        self.postgres_dest_bin_dir = os.path.join(self.dest_dir, 'postgres', 'bin')

        self.rpath_stats_by_dir = defaultdict(int)
        self.rpath_removal_stats_by_dir = defaultdict(int)

    @staticmethod
    def get_absolute_rpath_items(dest_root_dir: str) -> List[str]:
        """
        Get the list of absolute paths of subdirectories of the destination directory that should be
        accessible
        """
        assert not using_linuxbrew()
        return [
            os.path.abspath(os.path.join(dest_root_dir, 'lib', library_category.subdir_name))
            for library_category in CATEGORIES_FOR_RPATH
        ] + [os.path.abspath(os.path.join(dest_root_dir, 'postgres', 'lib'))]

    @staticmethod
    def get_relative_rpath_items(dest_root_dir: str, dest_abs_dir: str) -> List[str]:
        return [
            clean_path_join('$ORIGIN', os.path.relpath(rpath_item, dest_abs_dir))
            for rpath_item in LibraryPackager.get_absolute_rpath_items(dest_root_dir)
        ]

    def set_or_remove_rpath(self, dest_root_dir: str, file_path: str) -> None:
        if not should_manipulate_rpath_of(file_path):
            if not os.path.islink(file_path):
                logging.debug("Not manipulating RPATH or permissions of file: %s", file_path)
            return

        if not os.access(file_path, os.W_OK):
            # Make sure we can write to the file. This may be necessary for e.g. files copied from
            # Linuxbrew or third-party dependencies.
            subprocess.check_call(['chmod', 'u+w', file_path])
        if not os.access(file_path, os.X_OK):
            subprocess.check_call(['chmod', 'u+x', file_path])

        if using_linuxbrew():
            # When using Linuxbrew, we manage RPATH as follows:
            # - Remove RPATH altogether during packaging (what we are doing here).
            # - Generate a post_install.sh file, which, when executed, sets the RPATH appropriately
            #   according to the installation location.
            logging.debug("Removing RPATH from file: %s", file_path)
            self.rpath_removal_stats_by_dir[os.path.dirname(file_path)] += 1
            remove_rpath(file_path)
            return

        file_abs_path = os.path.abspath(file_path)
        new_rpath = ':'.join(LibraryPackager.get_relative_rpath_items(
                    dest_root_dir, os.path.dirname(file_abs_path)))
        logging.debug("Setting RPATH on file %s to %s", file_path, new_rpath)
        set_rpath(file_path, new_rpath)
        self.rpath_stats_by_dir[(os.path.dirname(file_path), new_rpath)] += 1

    def install_dyn_linked_binary(self, src_path: str, dest_dir: str) -> str:
        logging.debug(f"Installing dynamically-linked executable {src_path} to {dest_dir}")
        if not os.path.isdir(dest_dir):
            raise RuntimeError("Not a directory: '{}'".format(dest_dir))
        shutil.copy(src_path, dest_dir)
        installed_binary_path = os.path.join(dest_dir, os.path.basename(src_path))
        self.set_or_remove_rpath(self.dest_dir, installed_binary_path)
        self.installed_dyn_linked_binaries.append(installed_binary_path)
        return installed_binary_path

    def find_elf_dependencies(self, elf_file_path: str) -> Set[Dependency]:
        """
        Run ldd on the given ELF file and find libraries that it depends on. Also run patchelf and
        get the dynamic linker used by the file.

        @param elf_file_path: ELF file (executable/library) path
        """

        linuxbrew_home: Optional[LinuxbrewHome] = get_linuxbrew_home()
        elf_file_path = os.path.realpath(elf_file_path)
        if SYSTEM_LIBRARY_PATH_RE.match(elf_file_path) or not using_linuxbrew():
            ldd_path = '/usr/bin/ldd'
        else:
            assert linuxbrew_home is not None
            assert linuxbrew_home.ldd_path is not None
            ldd_path = linuxbrew_home.ldd_path

        ldd_result = run_program([ldd_path, elf_file_path], error_ok=True)
        dependencies: Set[Dependency] = set()

        ldd_result_stdout_str = ldd_result.stdout
        ldd_result_stderr_str = ldd_result.stderr
        if ldd_result.returncode != 0:
            # The below error message is printed to stdout on some platforms (CentOS) and
            # stderr on other platforms (Ubuntu).
            if 'not a dynamic executable' in (ldd_result_stdout_str, ldd_result_stderr_str):
                logging.debug(
                    "Not a dynamic executable: {}, ignoring dependency tracking".format(
                        elf_file_path))
                return dependencies
            raise RuntimeError(ldd_result.error_msg)

        for ldd_output_line in ldd_result_stdout_str.split("\n"):
            resolved_dep_match = RESOLVED_DEP_RE.match(ldd_output_line)
            if resolved_dep_match:
                lib_name = resolved_dep_match.group(1)
                lib_resolved_path = os.path.realpath(resolved_dep_match.group(2))
                dependencies.add(Dependency(lib_name, lib_resolved_path, elf_file_path,
                                            self.context))

            tokens = ldd_output_line.split()
            if len(tokens) >= 4 and tokens[1:4] == ['=>', 'not', 'found']:
                missing_lib_name = tokens[0]
                raise RuntimeError("Library not found for '{}': {}".format(
                    elf_file_path, missing_lib_name))

                # If we matched neither RESOLVED_DEP_RE or the "not found" case, that is still fine,
                # e.g. there could be a line of the following form in the ldd output:
                #   linux-vdso.so.1 =>  (0x00007ffc0f9d2000)

        return dependencies

    @staticmethod
    def is_postgres_binary(file_path: str) -> bool:
        return os.path.dirname(file_path).endswith('/postgres/bin')

    def get_dest_bin_dir_for_executable(self, file_path: str) -> str:
        if self.is_postgres_binary(file_path):
            dest_bin_dir = self.postgres_dest_bin_dir
        else:
            dest_bin_dir = self.main_dest_bin_dir
        return dest_bin_dir

    @staticmethod
    def join_binary_names_for_bash(binary_names: List[str]) -> str:
        return ' '.join(['"{}"'.format(name) for name in binary_names])

    def get_all_postgres_lib_deps(self) -> List[Dependency]:
        deps: List[Dependency] = []
        for root, dirs, files in os.walk(self.context.build_postgres_lib_dir):
            for file_name in files:
                if file_name.endswith('.so') and not os.path.islink(file_name):
                    file_path = os.path.join(root, file_name)
                    deps += self.find_elf_dependencies(file_path)
        return deps

    def get_postgres_lib_rel_paths_to_patch(self) -> List[str]:
        rel_paths: List[str] = []
        postgres_lib_dir = self.context.build_postgres_lib_dir
        for root, dirs, files in os.walk(postgres_lib_dir):
            for file_name in files:
                file_path = os.path.join(root, file_name)
                if file_name.endswith('.so') and not os.path.islink(file_name):
                    rel_paths.append(os.path.relpath(file_path, postgres_lib_dir))
        return rel_paths

    def package_binaries(self) -> None:
        """
        The main entry point to this class. Arranges binaries (executables and shared libraries),
        starting with the given set of "seed executables", in the destination directory so that
        the executables can find all of their dependencies.
        """

        linuxbrew_home = get_linuxbrew_home()

        all_deps: List[Dependency] = []

        dest_lib_dir = os.path.join(self.dest_dir, 'lib')
        mkdir_p(dest_lib_dir)

        mkdir_p(self.main_dest_bin_dir)
        mkdir_p(self.postgres_dest_bin_dir)

        main_elf_names_to_patch = []
        postgres_executable_names_to_patch = []
        postgres_lib_rel_paths_to_patch = self.get_postgres_lib_rel_paths_to_patch()

        glob_results_seen: Set[str] = set()
        processing_queue: Queue[str] = Queue()

        def add_glob_result(glob_result: str) -> None:
            abs_glob_result = os.path.abspath(glob_result)
            if abs_glob_result not in glob_results_seen:
                glob_results_seen.add(glob_result)
                processing_queue.put(abs_glob_result)

        for seed_executable_glob in self.seed_executable_patterns:
            glob_results = glob.glob(seed_executable_glob)
            if not glob_results:
                raise RuntimeError("No files found matching the pattern '{}'".format(
                    seed_executable_glob))
            for glob_result in glob_results:
                add_glob_result(glob_result)

        while not processing_queue.empty():
            executable = processing_queue.get()

            dest_bin_dir = self.get_dest_bin_dir_for_executable(executable)
            if os.path.islink(executable):
                link_target = os.readlink(executable)
                link_target_path = os.path.join(os.path.dirname(executable), link_target)
                abs_link_target = os.path.abspath(link_target_path)

                is_valid_link_target_basename = '/' not in link_target and '..' not in link_target
                link_target_exists = os.path.exists(abs_link_target)
                link_target_is_file = os.path.isfile(abs_link_target)
                if (not is_valid_link_target_basename or
                        not link_target_exists or
                        not link_target_is_file):
                    raise ValueError(
                        f"Invalid symlink target {link_target} of symlink {executable}: "
                        "only symlinks to existing files in same directory are allowed. "
                        f"Symlink target path: {link_target_path}. "
                        f"abs_link_target: {abs_link_target}. "
                        f"is_valid_link_target_basename: {is_valid_link_target_basename}. "
                        f"link_target_exists: {link_target_exists}. "
                        f"link_target_is_file: {link_target_is_file}.")

                symlink(link_target,
                        os.path.join(dest_bin_dir, os.path.basename(executable)))
                add_glob_result(link_target_path)
                continue

            if os.path.basename(os.path.dirname(os.path.abspath(executable))) == 'gobin':
                # This is a statically linked go binary
                shutil.copy(executable, dest_bin_dir)
                continue

            deps = self.find_elf_dependencies(executable)
            all_deps += deps
            if deps:
                self.install_dyn_linked_binary(executable, dest_bin_dir)
                executable_basename = os.path.basename(executable)
                if self.is_postgres_binary(executable):
                    postgres_executable_names_to_patch.append(executable_basename)
                else:
                    main_elf_names_to_patch.append(executable_basename)
            else:
                # This is probably a script.
                shutil.copy(executable, dest_bin_dir)

        all_deps += self.get_all_postgres_lib_deps()

        if using_linuxbrew():
            # Not using the install_dyn_linked_binary method for copying patchelf and ld.so as we
            # won't need to do any post-processing on these two later.
            assert linuxbrew_home is not None
            assert linuxbrew_home.patchelf_path is not None
            assert linuxbrew_home.ld_so_path is not None
            shutil.copy(linuxbrew_home.patchelf_path, self.main_dest_bin_dir)
            shutil.copy(linuxbrew_home.ld_so_path, dest_lib_dir)

        all_deps = sorted(set(all_deps))

        deps_sorted_by_name: List[Tuple[str, List[Dependency]]] = sorted_grouped_by(
            all_deps, lambda dep: dep.name)

        deps_with_same_name: List[Dependency]
        for dep_name, deps_with_same_name in deps_sorted_by_name:
            targets = sorted(set([dep.target for dep in deps_with_same_name]))
            if len(targets) > 1:
                raise RuntimeError(
                    "Multiple dependencies with the same name {} but different targets: {}".format(
                        dep_name, deps_with_same_name
                    ))

        linuxbrew_dest_dir = os.path.join(self.dest_dir, 'linuxbrew')
        linuxbrew_lib_dest_dir = os.path.join(linuxbrew_dest_dir, 'lib')

        # Add libresolv and libnss_* libs explicitly because they are loaded by glibc at runtime.
        additional_libs: Set[str] = set()
        if using_linuxbrew():
            for additional_lib_name_glob in ADDITIONAL_LIB_NAME_GLOBS:
                assert linuxbrew_home is not None
                assert linuxbrew_home.cellar_glibc_dir is not None
                additional_libs.update(
                    lib_path for lib_path in
                    glob.glob(os.path.join(
                        linuxbrew_home.cellar_glibc_dir,
                        '*',
                        'lib',
                        additional_lib_name_glob))
                    if not lib_path.endswith('.a'))

        for category, deps_in_category in sorted_grouped_by(all_deps,
                                                            lambda dep: dep.get_category()):
            logging.info("Found {} dependencies in category '{}':".format(
                len(deps_in_category), category))

            max_name_len = max([len(dep.name) for dep in deps_in_category])
            for dep in sorted(deps_in_category, key=lambda dep: dep.target):
                logging.info("    {} -> {}".format(
                    dep.name + ' ' * (max_name_len - len(dep.name)), dep.target))
            if category == DependencyCategory.SYSTEM:
                logging.info(
                    "Not packaging any of the above dependencies from a system-wide directory.")
                continue
            if category == 'postgres' and not using_linuxbrew():
                # Only avoid copying postgres libraries into the lib/yb directory in non-Linuxbrew
                # mode. In Linuxbrew mode, post_install.sh has complex logic for changing rpaths
                # and that is not updated to allow us to find libraries in postgres/lib.
                logging.info(
                    "Not installing any of the above YSQL libraries because they will be "
                    "installed as part of copying the entire postgres directory.")
                continue

            if category == DependencyCategory.LINUXBREW:
                category_dest_dir = linuxbrew_lib_dest_dir
            else:
                category_dest_dir = os.path.join(dest_lib_dir, category.subdir_name)
            mkdir_p(category_dest_dir)

            for dep in deps_in_category:
                additional_libs.discard(dep.target)
                self.install_dyn_linked_binary(dep.target, category_dest_dir)
                target_name = os.path.basename(dep.target)
                if target_name != dep.name:
                    target_src = os.path.join(os.path.dirname(dep.target), dep.name)
                    additional_libs.discard(target_src)
                    symlink(target_name, os.path.join(category_dest_dir, dep.name))

        for lib_path in additional_libs:
            if os.path.isfile(lib_path):
                self.install_dyn_linked_binary(lib_path, linuxbrew_lib_dest_dir)
                logging.info("Installed additional lib: " + lib_path)
            elif os.path.islink(lib_path):
                link_target_basename = os.path.basename(os.readlink(lib_path))
                logging.info("Installed additional symlink: " + lib_path)
                symlink(link_target_basename,
                        os.path.join(linuxbrew_lib_dest_dir, os.path.basename(lib_path)))
            else:
                raise RuntimeError(
                    "Expected '{}' to be a file or a symlink".format(lib_path))

        for installed_binary in self.installed_dyn_linked_binaries:
            # Sometimes files that we copy from other locations are not even writable by user!
            subprocess.check_call(['chmod', 'u+w', installed_binary])

        post_install_path = os.path.join(self.main_dest_bin_dir, 'post_install.sh')

        if using_linuxbrew():
            # Add other files used by glibc at runtime.
            assert linuxbrew_home is not None

            linuxbrew_dir = linuxbrew_home.linuxbrew_dir
            assert linuxbrew_dir is not None
            assert linuxbrew_home.ldd_path is not None

            linuxbrew_glibc_real_path = os.path.normpath(
                os.path.join(os.path.realpath(linuxbrew_home.ldd_path), '..', '..'))

            assert linuxbrew_home is not None
            linuxbrew_glibc_rel_path = os.path.relpath(
                linuxbrew_glibc_real_path, os.path.realpath(linuxbrew_dir))

            # We expect glibc to live under a path like "Cellar/glibc/3.23" in
            # the Linuxbrew directory.
            if not linuxbrew_glibc_rel_path.startswith('Cellar/glibc/'):
                raise ValueError(
                    "Expected to find glibc under Cellar/glibc/<version> in Linuxbrew, but found it"
                    " at: '%s'" % linuxbrew_glibc_rel_path)

            rel_paths = []
            for glibc_rel_path in [
                'etc/ld.so.cache',
                'etc/localtime',
                'lib/locale/locale-archive',
                'lib/gconv',
                'libexec/getconf',
                'share/locale',
                'share/zoneinfo',
            ]:
                rel_paths.append(os.path.join(linuxbrew_glibc_rel_path, glibc_rel_path))

            terminfo_glob_pattern = os.path.join(
                    linuxbrew_dir, 'Cellar/ncurses/*/share/terminfo')
            terminfo_paths = glob.glob(terminfo_glob_pattern)
            if len(terminfo_paths) != 1:
                raise ValueError(
                    "Failed to find the terminfo directory using glob pattern %s. "
                    "Found: %s" % (terminfo_glob_pattern, terminfo_paths))
            terminfo_rel_path = os.path.relpath(terminfo_paths[0], linuxbrew_dir)
            rel_paths.append(terminfo_rel_path)

            for rel_path in rel_paths:
                src = os.path.join(linuxbrew_dir, rel_path)
                dst = os.path.join(linuxbrew_dest_dir, rel_path)
                copy_deep(src, dst, create_dst_dir=True)

            with open(post_install_path) as post_install_script_input:
                post_install_script = post_install_script_input.read()

            new_post_install_script = post_install_script
            replacements = [
                ("original_linuxbrew_path_to_patch", linuxbrew_dir),
                ("original_linuxbrew_path_length", len(linuxbrew_dir)),
            ]
            for macro_var_name, list_of_binary_names in [
                ("main_elf_names_to_patch", main_elf_names_to_patch),
                ("postgres_executable_names_to_patch", postgres_executable_names_to_patch),
                ("postgres_lib_rel_paths_to_patch", postgres_lib_rel_paths_to_patch),
            ]:
                replacements.append(
                    (macro_var_name, self.join_binary_names_for_bash(list_of_binary_names)))

            for macro_var_name, value in replacements:
                new_post_install_script = new_post_install_script.replace(
                    '${%s}' % macro_var_name, str(value))
        else:
            new_post_install_script = (
                    '#!/usr/bin/env bash\n'
                    '# For backward-compatibility only. We do not need this script anymore.\n'
                )

        with open(post_install_path, 'w') as post_install_script_output:
            post_install_script_output.write(new_post_install_script)

        self.log_rpath_stats()

    def log_rpath_stats(self) -> None:
        for dir_path, count in self.rpath_removal_stats_by_dir.items():
            logging.info("Removed RPATH from %d files in %s", count, dir_path)
        for (dir_path, rpath), count in self.rpath_stats_by_dir.items():
            logging.info("Set RPATH to %s for %d files in %s", rpath, count, dir_path)

    def postprocess_distribution(self, build_target: str) -> None:
        """
        build_target is different from self.dest_dir because this function is invoked after
        PostgreSQL files are copied to the second packaging directory. Ideally there should only
        be one intermediate packaging directory, and we should only have one pass to set rpath
        on all executables and dynamic libraries.
        """
        for root, dirs, files in os.walk(os.path.join(build_target, 'postgres')):
            for file_name in files:
                file_path = os.path.join(root, file_name)
                self.set_or_remove_rpath(build_target, file_path)
