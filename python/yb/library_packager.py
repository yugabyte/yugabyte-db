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

"""
Copyright (c) YugaByte, Inc.

Finds all Linux dynamic libraries that have to be packaged with the YugaByte distribution tarball by
starting from a small set of executables and walking the dependency graph. Creates a self-sufficient
distribution directory.

Run doctest tests as follows:

  python -m doctest python/yb/library_packager.py
"""


import argparse
import collections
import glob
import json
import logging
import os
import re
import shutil
import subprocess
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from yb.command_util import run_program, mkdir_p  # nopep8
from yb.linuxbrew import get_linuxbrew_dir  # nopep8
from yb.common_util import YB_THIRDPARTY_DIR, YB_SRC_ROOT, sorted_grouped_by, \
                           safe_path_join  # nopep8


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
LINUXBREW_HOME = get_linuxbrew_dir()
LINUXBREW_CELLAR_GLIBC_DIR = safe_path_join(LINUXBREW_HOME, 'Cellar', 'glibc')
ADDITIONAL_LIB_NAME_GLOBS = ['libnss_*', 'libresolv*']
LINUXBREW_LDD_PATH = safe_path_join(LINUXBREW_HOME, 'bin', 'ldd')

YB_SCRIPT_BIN_DIR = os.path.join(YB_SRC_ROOT, 'bin')
YB_BUILD_SUPPORT_DIR = os.path.join(YB_SRC_ROOT, 'build-support')

PATCHELF_NOT_AN_ELF_EXECUTABLE = 'not an ELF executable'
PATCHELF_PATH = safe_path_join(LINUXBREW_HOME, 'bin', 'patchelf')

LIBRARY_PATH_RE = re.compile('^(.*[.]so)(?:$|[.].*$)')
LIBRARY_CATEGORIES = ['system', 'yb', 'yb-thirdparty', 'linuxbrew']


# This is an alternative to global variables, bundling a few commonly used things.
DistributionContext = collections.namedtuple(
        'DistributionContext',
        ['build_dir',
         'dest_dir',
         'verbose_mode'])


class Dependency:
    """
    Describes a dependency of an executable or a shared library on another shared library.
    @param name: the name of the library as requested by the original executable/shared library
    @param target: target file pointed to by the dependency
    """
    def __init__(self, name, target, origin, context):
        self.name = name
        self.target = target
        self.category = None
        self.context = context
        self.origin = origin

    def __hash__(self):
        return hash(self.name) ^ hash(self.target)

    def __eq__(self, other):
        return self.name == other.name and \
               self.target == other.target

    def __str__(self):
        return "Dependency(name='{}', target='{}', origin='{}')".format(
                self.name, self.target, self.origin)

    def __repr__(self):
        return str(self)

    def __cmp__(self, other):
        return (self.name, self.target) < (other.name, other.target)

    def get_category(self):
        """
        Categorizes binaries into a few buckets:
        - yb -- YugaByte product itself
        - yb-thirdparty -- built with YugaByte
        - linuxbrew -- built using Linuxbrew
        - system -- grabbed from a system-wide library directory
        """
        if self.category:
            return self.category

        if self.target.startswith(LINUXBREW_HOME + '/'):
            self.category = 'linuxbrew'
        elif self.target.startswith(YB_THIRDPARTY_DIR + '/'):
            self.category = 'yb-thirdparty'
        elif self.target.startswith(self.context.build_dir + '/'):
            self.category = 'yb'
        elif (self.target.startswith(YB_SCRIPT_BIN_DIR + '/') or
              self.target.startswith(YB_BUILD_SUPPORT_DIR + '/')):
            self.category = 'yb-scripts'

        if not self.category:
            for system_library_path in SYSTEM_LIBRARY_PATHS:
                if self.target.startswith(system_library_path + '/'):
                    self.category = 'system'
                    break

        if self.category:
            if self.category not in LIBRARY_CATEGORIES:
                raise RuntimeError(
                    ("Internal error: library category computed as '{}', must be one of: {}. " +
                     "Dependency: {}").format(self.category, LIBRARY_CATEGORIES, self))
            return self.category

        raise RuntimeError(
            ("Could not determine the category of this binary "
             "(yugabyte / yb-thirdparty / linuxbrew / system): '{}'. "
             "Does not reside in the Linuxbrew directory ('{}'), "
             "YB third-party directory ('{}'), "
             "YB build directory ('{}'), "
             "YB general-purpose script directory ('{}'), "
             "YB build support script directory ('{}'), "
             "and does not appear to be a system library (does not start with any of {})."
             ).format(self.target, LINUXBREW_HOME, YB_THIRDPARTY_DIR, self.context.build_dir,
                      YB_SCRIPT_BIN_DIR, YB_BUILD_SUPPORT_DIR, SYSTEM_LIBRARY_PATHS))


def add_common_arguments(parser):
    """
    Add command-line arguments common between library_packager_old.py invoked as a script, and
    the yb_release.py script.
    """
    parser.add_argument('--verbose',
                        help='Enable verbose output.',
                        action='store_true')


def run_patchelf(*args):
    patchelf_result = run_program([PATCHELF_PATH] + list(args), error_ok=True)
    if patchelf_result.returncode != 0 and patchelf_result.stderr not in [
            'cannot find section .interp',
            'cannot find section .dynamic',
            PATCHELF_NOT_AN_ELF_EXECUTABLE]:
        raise RuntimeError(patchelf_result.error_msg)
    return patchelf_result


def symlink(source, link_path):
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
    def __init__(self,
                 build_dir,
                 seed_executable_patterns,
                 dest_dir,
                 verbose_mode=False):
        build_dir = os.path.realpath(build_dir)
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
            verbose_mode=verbose_mode)
        self.installed_dyn_linked_binaries = []

    def install_dyn_linked_binary(self, src_path, dest_dir):
        if not os.path.isdir(dest_dir):
            raise RuntimeError("Not a directory: '{}'".format(dest_dir))
        shutil.copy(src_path, dest_dir)
        installed_binary_path = os.path.join(dest_dir, os.path.basename(src_path))
        self.installed_dyn_linked_binaries.append(installed_binary_path)
        return installed_binary_path

    def find_elf_dependencies(self, elf_file_path):
        """
        Run ldd on the given ELF file and find libraries that it depends on. Also run patchelf and
        get the dynamic linker used by the file.

        @param elf_file_path: ELF file (executable/library) path
        """

        elf_file_path = os.path.realpath(elf_file_path)
        if SYSTEM_LIBRARY_PATH_RE.match(elf_file_path):
            ldd_path = '/usr/bin/ldd'
        else:
            ldd_path = LINUXBREW_LDD_PATH

        ldd_result = run_program([ldd_path, elf_file_path], error_ok=True)
        dependencies = set()

        if ldd_result.returncode != 0:
            # Interestingly, the below error message is printed to stdout, not stderr.
            if ldd_result.stdout == 'not a dynamic executable':
                logging.debug(
                    "Not a dynamic executable: {}, ignoring dependency tracking".format(
                        elf_file_path))
                return dependencies
            raise RuntimeError(ldd_result.error_msg)

        for ldd_output_line in ldd_result.stdout.split("\n"):
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

    def package_binaries(self):
        """
        The main entry point to this class. Arranges binaries (executables and shared libraries),
        starting with the given set of "seed executables", in the destination directory so that
        the executables can find all of their dependencies.
        """
        all_deps = []

        executables = []

        dest_bin_dir = os.path.join(self.dest_dir, 'bin')
        mkdir_p(dest_bin_dir)

        dest_lib_dir = os.path.join(self.dest_dir, 'lib')
        mkdir_p(dest_lib_dir)

        unwrapped_bin_dir = os.path.join(dest_lib_dir, 'unwrapped')
        mkdir_p(unwrapped_bin_dir)

        # We still need to use LD_LIBRARY_PATH because otherwise libc fails to find libresolv at
        # runtime.
        ld_library_path = ':'.join(
                ["${BASH_SOURCE%/*}/../lib/" + category for category in LIBRARY_CATEGORIES])

        elf_names_to_set_interpreter = []
        for seed_executable_glob in self.seed_executable_patterns:
            updated_glob = seed_executable_glob.replace('$BUILD_ROOT', self.context.build_dir)
            if updated_glob != seed_executable_glob:
                logging.info("Substituting: {} -> {}".format(seed_executable_glob, updated_glob))
                seed_executable_glob = updated_glob
            glob_results = glob.glob(seed_executable_glob)
            if not glob_results:
                raise RuntimeError("No files found matching the pattern '{}'".format(
                    seed_executable_glob))
            for executable in glob_results:
                deps = self.find_elf_dependencies(executable)
                all_deps += deps
                if deps:
                    installed_binary_path = self.install_dyn_linked_binary(executable,
                                                                           unwrapped_bin_dir)
                    executable_basename = os.path.basename(executable)
                    wrapper_script_path = os.path.join(dest_bin_dir, executable_basename)
                    with open(wrapper_script_path, 'w') as wrapper_script_file:
                        wrapper_script_file.write(
                            "#!/usr/bin/env bash\n"
                            "LD_LIBRARY_PATH=" + ld_library_path + " exec "
                            "${BASH_SOURCE%/*}/../lib/unwrapped/" + executable_basename + ' "$@"')
                    os.chmod(wrapper_script_path, 0755)
                    elf_names_to_set_interpreter.append(executable_basename)
                else:
                    # This is probably a script.
                    shutil.copy(executable, dest_bin_dir)

        # Not using the install_dyn_linked_binary method for copying patchelf and ld.so as we won't
        # need to do any post-processing on these two later.
        shutil.copy(PATCHELF_PATH, dest_bin_dir)

        ld_path = os.path.join(LINUXBREW_HOME, 'lib', 'ld.so')
        shutil.copy(ld_path, dest_lib_dir)

        all_deps = sorted(set(all_deps))

        for dep_name, deps in sorted_grouped_by(all_deps, lambda dep: dep.name):
            targets = sorted(set([dep.target for dep in deps]))
            if len(targets) > 1:
                raise RuntimeError(
                    "Multiple dependencies with the same name {} but different targets: {}".format(
                        dep_name, deps
                    ))

        categories = sorted(set([dep.get_category() for dep in all_deps]))

        for category, deps_in_category in sorted_grouped_by(all_deps,
                                                            lambda dep: dep.get_category()):
            logging.info("Found {} dependencies in category '{}':".format(
                len(deps_in_category), category))

            max_name_len = max([len(dep.name) for dep in deps_in_category])
            for dep in sorted(deps_in_category, key=lambda dep: dep.target):
                logging.info("    {} -> {}".format(
                    dep.name + ' ' * (max_name_len - len(dep.name)), dep.target))

            category_dest_dir = os.path.join(dest_lib_dir, category)
            mkdir_p(category_dest_dir)

            for dep in deps_in_category:
                self.install_dyn_linked_binary(dep.target, category_dest_dir)
                target_basename = os.path.basename(dep.target)
                if os.path.basename(dep.target) != dep.name:
                    symlink(os.path.basename(dep.target),
                            os.path.join(category_dest_dir, dep.name))

        linuxbrew_lib_dest_dir = os.path.join(dest_lib_dir, 'linuxbrew')

        # Add libresolv and libnss_* libraries explicitly because they are loaded by glibc at
        # runtime and will not be discovered automatically using ldd.
        for additional_lib_name_glob in ADDITIONAL_LIB_NAME_GLOBS:
            for lib_path in glob.glob(os.path.join(LINUXBREW_CELLAR_GLIBC_DIR, '*', 'lib',
                                                   additional_lib_name_glob)):
                lib_basename = os.path.basename(lib_path)
                if lib_basename.endswith('.a'):
                    continue
                if os.path.isfile(lib_path):
                    self.install_dyn_linked_binary(lib_path, linuxbrew_lib_dest_dir)
                elif os.path.islink(lib_path):
                    link_target_basename = os.path.basename(os.readlink(lib_path))
                    symlink(link_target_basename,
                            os.path.join(linuxbrew_lib_dest_dir, lib_basename))
                else:
                    raise RuntimeError(
                        "Expected '{}' to be a file or a symlink".format(lib_path))

        for installed_binary in self.installed_dyn_linked_binaries:
            # Sometimes files that we copy from other locations are not even writable by user!
            subprocess.check_call(['chmod', 'u+w', installed_binary])
            # Remove rpath so that it does not interfere with the LD_LIBRARY_PATH mechanism,
            # and because some libraries might also have system library directories on their rpath.
            run_patchelf('--remove-rpath', installed_binary)

        post_install_path = os.path.join(dest_bin_dir, 'post_install.sh')
        with open(post_install_path) as post_install_script_input:
            post_install_script = post_install_script_input.read()
        binary_names_to_patch = ' '.join([
            '"{}"'.format(name) for name in elf_names_to_set_interpreter])
        new_post_install_script = post_install_script.replace('${elf_names_to_set_interpreter}',
                                                              binary_names_to_patch)
        with open(post_install_path, 'w') as post_install_script_output:
            post_install_script_output.write(new_post_install_script)


if __name__ == '__main__':
    if not os.path.isdir(YB_THIRDPARTY_DIR):
        raise RuntimeError("Third-party dependency directory '{}' does not exist".format(
            YB_THIRDPARTY_DIR))

    parser = argparse.ArgumentParser(description=LibraryPackager.__doc__)
    parser.add_argument('--build-dir',
                        help='Build directory to pick up executables/libraries from.',
                        required=True)
    parser.add_argument('--dest-dir',
                        help='Destination directory to save the self-sufficient directory tree '
                             'of executables and libraries at.',
                        required=True)
    parser.add_argument('--clean-dest',
                        help='Remove the destination directory if it already exists. Only works '
                             'with directories under /tmp/... for safety.',
                        action='store_true')
    add_common_arguments(parser)

    args = parser.parse_args()
    log_level = logging.INFO
    if args.verbose:
        log_level = logging.DEBUG
    logging.basicConfig(
        level=log_level,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    release_manifest_path = os.path.join(YB_SRC_ROOT, 'yb_release_manifest.json')
    release_manifest = json.load(open(release_manifest_path))
    if args.clean_dest and os.path.exists(args.dest_dir):
        if args.dest_dir.startswith('/tmp/'):
            logging.info(("--clean-dest specified and '{}' already exists, "
                          "deleting.").format(args.dest_dir))
            shutil.rmtree(args.dest_dir)
        else:
            raise RuntimeError(
                    "For safety, --clean-dest only works with destination directories "
                    "under /tmp/...")

    packager = LibraryPackager(build_dir=args.build_dir,
                               seed_executable_patterns=release_manifest['bin'],
                               dest_dir=args.dest_dir,
                               verbose_mode=args.verbose)
    packager.package_binaries()
