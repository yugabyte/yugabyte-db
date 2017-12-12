# Copyright (c) YugaByte, Inc.

import glob
import logging
import os
import shutil
import stat
import subprocess
import sys

from yb.command_util import run_program


def add_common_arguments(parser):
    """
    Add command-line arguments common between library_packager_old.py invoked as a script, and
    the yb_release.py script.
    """
    parser.add_argument('--verbose',
                        help='Enable verbose output.',
                        action='store_true')


class MacLibraryPackager:
    def __init__(self,
                 build_dir,
                 seed_executable_patterns,
                 dest_dir,
                 verbose_mode=False):
        self.build_dir = os.path.realpath(build_dir)
        if not os.path.exists(self.build_dir):
            raise IOError("Build directory '{}' does not exist".format(self.build_dir))
        self.seed_executable_patterns = seed_executable_patterns
        self.dest_dir = dest_dir
        logging.debug(
            "Traversing the dependency graph of executables/libraries, starting "
            "with seed executable patterns: {}".format(", ".join(seed_executable_patterns)))
        self.nodes_by_digest = {}
        self.nodes_by_path = {}

        self.dest_dir = dest_dir
        self.verbose_mode = verbose_mode

    def package_binaries(self):
        src = self.build_dir
        dst = self.dest_dir

        dst_bin_dir = os.path.join(dst, "bin")
        dst_lib_dir = os.path.join(dst, "lib")

        try:
            os.makedirs(dst_bin_dir)
        except OSError as e:
            raise RuntimeError("Unable to create dir %s" % dst)

        logging.debug("Created directory %s" % dst)

        for seed_executable_glob in self.seed_executable_patterns:
            updated_glob = seed_executable_glob.replace('$BUILD_ROOT', self.build_dir)
            if updated_glob != seed_executable_glob:
                logging.info("Substituting: {} -> {}".format(seed_executable_glob, updated_glob))
                seed_executable_glob = updated_glob
            glob_results = glob.glob(seed_executable_glob)
            if not glob_results:
                raise RuntimeError("No files found matching the pattern '{}'".format(
                    seed_executable_glob))
            for executable in glob_results:
                shutil.copy(executable, dst_bin_dir)

        for bin_file in os.listdir(dst_bin_dir):
            if bin_file.endswith(".sh"):
                continue

            logging.debug("Processing binary file: %s" % bin_file)
            libs = []
            processed_libs = []
            os.makedirs(os.path.join(dst, "lib", bin_file))
            libs = self.fix_load_paths(os.path.join(dst_bin_dir, bin_file),
                                       os.path.join(dst_lib_dir, bin_file),
                                       os.path.join("@loader_path/../lib/", bin_file))

            # Elements in libs are absolute paths.
            logging.info("library dependencies for file %s: %s" % (bin_file, libs))
            for lib in libs:
                if lib in processed_libs:
                    continue

                logging.debug("processing library: %s" % lib)
                libname = os.path.basename(lib)
                lib_dir_path = os.path.join(dst, "lib", libname)
                if os.path.exists(lib_dir_path):
                    continue

                os.mkdir(lib_dir_path)
                shutil.copy(lib, lib_dir_path)

                lib_file_path = os.path.join(lib_dir_path, libname)

                self.fix_load_paths(lib_file_path, lib_dir_path, "@loader_path")
                processed_libs.append(lib)

    def extract_rpaths(self, filename):
        result = run_program(["otool", "-l", filename])

        rpaths = []
        lines = result.stdout.splitlines()
        for idx, line in enumerate(lines):
            # Extract rpath. Sample output from 'otool -l':
            # Load command 78
            #          cmd LC_RPATH
            #      cmdsize 72
            #         path /Users/hector/code/yugabyte/thirdparty/installed/common/lib (offset 12)
            if line.strip() == 'cmd LC_RPATH':
                path_line = lines[idx + 2]
                if path_line.split()[0] != "path":
                    raise RuntimeError("Invalid output from 'otool -l %s'. "
                                       "Expecting line to start with 'path'. Got '%s'" %
                                       (filename, path_line.split()[0]))
                rpaths.append(path_line.split()[1])

        return rpaths

    def extract_dependency_paths(self, filename, rpaths):
        output = run_program(["otool", "-L", filename]).stdout

        if len(output) > 0 and output[0].endswith("is not an object file"):
            return [], []

        dependency_paths = []
        absolute_dependency_paths = []
        lines = output.splitlines()
        # Skip the first line that is always the library path.
        for line in lines[1:]:
            path = line.split()[0]

            # The paths extracted by using otool -L can be absolute paths or relative paths starting
            # with @rpath or @loader_path. Example:
            # otool -L ./build/debug-clang-dynamic-enterprise/lib/libmaster.dylib
            # ./build/debug-clang-dynamic-enterprise/lib/libmaster.dylib:
            #    @rpath/libmaster.dylib (compatibility version 0.0.0, current version 0.0.0)
            #    @rpath/libtserver.dylib (compatibility version 0.0.0, current version 0.0.0)
            #    @rpath/libtablet.dylib (compatibility version 0.0.0, current version 0.0.0)
            #    /Users/hector/code/yugabyte/thirdparty/installed/uninstrumented/lib/\
            # libsnappy.1.dylib (compatibility version 3.0.0, current version 3.4.0)
            #    /usr/lib/libbz2.1.0.dylib (compatibility version 1.0.0, current version 1.0.5)
            #    /usr/lib/libz.1.dylib (compatibility version 1.0.0, current version 1.2.8)
            #
            # So we want to find the absolute paths of those paths that start with @rpath by trying
            # all the rpaths extracted by using 'otool -l'

            # If we don't skip system libraries and package it, macOS will complain that the library
            # exists in two different places (in /usr/lib and in our package lib directory).
            if path.startswith("/usr/lib"):
                continue
            if path.startswith('@rpath'):
                name = os.path.basename(path)
                # Find the absolute path by prepending all the rpaths extracted from the file.
                for rpath in rpaths:
                    candidate_path = os.path.join(rpath, name)
                    if os.path.isfile(candidate_path):
                        absolute_dependency_paths.append(candidate_path)
                        break
            elif not path.startswith('@loader_path'):
                # This should be an absolute path.
                if os.path.isfile(path):
                    absolute_dependency_paths.append(path)
                else:
                    raise RuntimeError("File %s doesn't exist" % path)

            dependency_paths.append(path)

        return dependency_paths, absolute_dependency_paths

    def remove_rpaths(self, filename, rpaths):
        for rpath in rpaths:
            run_program(["install_name_tool", "-delete_rpath", rpath, filename])
            logging.info("Successfully removed rpath %s from %s" % (rpath, filename))

    def set_new_path(self, filename, old_path, new_path):
        # We need to use a different command if the path is pointing to itself. Example:
        # otool - L ./build/debug-clang-dynamic-enterprise/lib/libmaster.dylib
        # ./build/debug-clang-dynamic-enterprise/ lib/libmaster.dylib:
        #      @rpath/libmaster.dylib

        cmd = []
        if os.path.basename(filename) == os.path.basename(old_path):
            run_program(["install_name_tool", "-id", new_path, filename])
            logging.debug('install_name_tool -id %s %s' % (new_path, filename))
        else:
            run_program(["install_name_tool", "-change", old_path, new_path, filename])
            logging.debug('install_name_tool -change %s %s %s' % (old_path, new_path, filename))

    def fix_load_paths(self, filename, lib_bin_dir, loader_path):
        logging.debug('Processing file %s' % filename)

        original_mode = os.stat(filename).st_mode
        # Made the file writable.
        try:
            os.chmod(filename, os.stat(filename).st_mode | stat.S_IWUSR)
        except OSError as e:
            logging.error("Unable to make file % writable" % filename)
            raise

        # Extract the paths that are used to resolve paths that start with @rpath.
        rpaths = self.extract_rpaths(filename)
        logging.debug('rpaths for filel %s: %s' % (filename, rpaths))

        # Remove rpaths since we are only going to use @loader_path and absolute paths.
        self.remove_rpaths(filename, rpaths)

        # Dependency path will have the paths as extracted by 'otool -L'.
        dependency_paths, absolute_dependency_paths = self.extract_dependency_paths(filename,
                                                                                    rpaths)

        logging.debug('absolute_dependency_paths for file %s: %s' % (filename,
                      absolute_dependency_paths))

        # Prepend @loader_path to all dependency paths.
        for dependency_path in dependency_paths:
            basename = os.path.basename(dependency_path)
            new_path = os.path.join(loader_path, basename)

            self.set_new_path(filename, dependency_path, new_path)

        logging.debug('absolute_paths: %s' % absolute_dependency_paths)

        # For each library dependency, check whether it already has its own directory (if it does,
        # a physical copy of this library must exist there). If it doesn't, create it and copy the
        # physical file there.
        for absolute_path in absolute_dependency_paths:
            # Don't copy the file again.
            lib_file_name = os.path.basename(absolute_path)
            relative_lib_path = os.path.join("..", lib_file_name, lib_file_name)

            # Create symlink in lib_bin_dir.
            symlink_path = os.path.join(lib_bin_dir, lib_file_name)
            if not os.path.exists(symlink_path):
                logging.info('Creating symlink %s' % symlink_path)
                os.symlink(relative_lib_path, symlink_path)

        # Restore the file's mode.
        try:
            os.chmod(filename, original_mode)
        except OSError as e:
            logging.error('Unable to restore file %s mode' % filename)
            raise

        return absolute_dependency_paths
