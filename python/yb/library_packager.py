#!/usr/bin/env python


"""
Copyright (c) YugaByte, Inc.

Finds all Linux dynamic libraries that have to be packaged with the YugaByte distribution tarball
by starting from a small set of executables and walking the dependency graph.

Run doctest tests as follows:

  python -m doctest python/yb/library_packager.py

"""

import argparse
import filecmp
import glob
import hashlib
import json
import logging
import os
import re
import shutil
import stat
import sys
import yaml

from collections import deque, defaultdict
from os.path import basename as path_basename
from os.path import dirname as path_dirname
from os.path import join as path_join
from os.path import realpath
from distutils.dir_util import mkpath
from yb.command_util import run_program


# A resolved shared library dependency shown by ldd.
# Example (split across two lines):
#   libmaster.so => /home/mbautin/code/yugabyte/build/debug-gcc-dynamic/lib/libmaster.so
#   (0x00007f941fa5f000)
RESOLVED_DEP_RE = re.compile(r'^\s*(\S+)\s+=>\s+(\S.*\S)\s+[(]')

# This is what "readelf -d" prints for RPATH/RUNPATH entries.
# Example:
#  0x000000000000000f (RPATH)              Library rpath: [<colon_separated_paths>]
READELF_RPATH_RE = re.compile(r'Library (?:rpath|runpath): \[(.+)\]')

HOME_DIR = os.path.expanduser('~')
LINUXBREW_HOME = os.path.join(HOME_DIR, '.linuxbrew-yb-build')
PATCHELF_PATH = path_join(LINUXBREW_HOME, 'bin', 'patchelf')
LINUXBREW_LDD_PATH = path_join(LINUXBREW_HOME, 'bin', 'ldd')

PATCHELF_NOT_AN_ELF_EXECUTABLE = 'not an ELF executable'

# This module is expected to be in the "python/yb" directory.
MODULE_DIR = path_dirname(realpath(__file__))
YB_SRC_ROOT = realpath(path_join(MODULE_DIR, '..', '..'))


class Dependency:
    """
    Describes a dependency of an executable or a shared library on another shared library.
    @param name: the name of the library as requested by the dependee
    @param target: target file pointed to by the dependency
    @param via_dlopen: dependencies loaded via dlopen have to be explicitly linked into each
                       library/executable's directory that is added to RPATH.
    """
    def __init__(self, name, target, via_dlopen=False):
        self.name = name
        self.target = target
        self.via_dlopen = via_dlopen

    def __hash__(self):
        return hash(self.name) ^ hash(self.target) ^ hash(self.via_dlopen)

    def __eq__(self, other):
        return self.name == other.name and \
               self.target == other.target and \
               self.via_dlopen == other.via_dlopen

    def __str__(self):
        return "Dependency({}, {}, via_dlopen={})".format(
                repr(self.name), repr(self.target), self.via_dlopen)

    def __cmp__(self, other):
        return (self.name, self.target, self.via_dlopen) < \
               (other.name, other.target, other.via_dlopen)

    def as_metadata(self):
        """
        @return a dictionary representing this dependency's entry suitable for inclusion into
                metadata files that we provide for ease of debugging along with each executable
                and shared library.
        """
        return dict(name=self.name,
                    original_location=normalize_path_for_metadata(self.target),
                    via_dlopen=self.via_dlopen)


def run_patchelf(*args):
    patchelf_result = run_program([PATCHELF_PATH] + list(args), error_ok=True)
    if patchelf_result.returncode != 0 and patchelf_result.stderr not in [
            'cannot find section .interp',
            'cannot find section .dynamic',
            PATCHELF_NOT_AN_ELF_EXECUTABLE]:
        raise RuntimeError(patchelf_result.error_msg)
    return patchelf_result


def normalize_path_for_metadata(path):
    if path.startswith(HOME_DIR + '/'):
        return '~/' + path[len(HOME_DIR) + 1:]
    return path


def find_elf_dependencies(elf_file_path):
    """
    Run ldd on the given ELF file and find libraries that it depends on. Also run patchelf and get
    the dynamic linker used by the file.

    @param elf_file_path: ELF file (executable/library) path
    """

    elf_file_path = realpath(elf_file_path)
    if elf_file_path.startswith('/usr/') or elf_file_path.startswith('/lib64/'):
        ldd_path = '/usr/bin/ldd'
    else:
        ldd_path = LINUXBREW_LDD_PATH

    ldd_result = run_program([ldd_path, elf_file_path], error_ok=True)
    dependencies = set()
    if ldd_result.returncode != 0:
        # Interestingly, the below error message is printed to stdout, not stderr.
        if ldd_result.stdout == 'not a dynamic executable':
            logging.debug(
                "Not a dynamic executable: {}, ignoring dependency tracking".format(elf_file_path))
            return dependencies
        raise RuntimeError(ldd_result.error_msg)

    for ldd_output_line in ldd_result.stdout.split("\n"):
        m = RESOLVED_DEP_RE.match(ldd_output_line)
        if m:
            lib_name = m.group(1)
            lib_resolved_path = realpath(m.group(2))
            dependencies.add(Dependency(lib_name, lib_resolved_path))

        tokens = ldd_output_line.split()
        if len(tokens) >= 4 and tokens[1:4] == ['=>', 'not', 'found']:
            missing_lib_name = tokens[0]
            raise RuntimeError("Library not found for '{}': {}".format(
                elf_file_path, missing_lib_name))

        # If we matched neither RESOLVED_DEP_RE or the "not found" case, that is still fine,
        # e.g. there could be a line of the following form in the ldd output:
        #   linux-vdso.so.1 =>  (0x00007ffc0f9d2000)

    elf_basename = path_basename(elf_file_path)
    elf_dirname = path_dirname(elf_file_path)
    if elf_basename.startswith('libsasl2.'):
        # TODO: don't package Berkeley DB with the product -- it has an AGPL license.
        for libdb_so_name in ['libdb.so', 'libdb-5.so']:
            libdb_so_path = path_join(path_dirname(elf_file_path), 'libdb.so')
            if os.path.exists(libdb_so_path):
                dependencies.add(Dependency('libdb.so', libdb_so_path, via_dlopen=True))
        sasl_plugin_dir = '/usr/lib64/sasl2'
        for sasl_lib_name in os.listdir(sasl_plugin_dir):
            if sasl_lib_name.endswith('.so'):
                dependencies.add(
                        Dependency(sasl_lib_name,
                                   realpath(path_join(sasl_plugin_dir, sasl_lib_name))))

    if elf_basename.startswith('libc-'):
        # glibc loads a lot of libns_... libraries using dlopen.
        for libnss_lib in glob.glob(os.path.join(elf_dirname, 'libnss_*')):
            if re.search(r'([.]so|\d+)$', libnss_lib):
                dependencies.add(Dependency(path_basename(libnss_lib),
                                            libnss_lib, via_dlopen=True))

    return dependencies


def compute_file_digest(file_path):
    digest_calculator = hashlib.sha256()

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(1048576)
            if not data:
                break
            digest_calculator.update(data)
    return digest_calculator.hexdigest()


def remove_so_extension_from_lib_name(lib_name):
    """
    Transforms a library name into something suitable for naming a directory that will contain
    symlinks to all its dependencies, metadata file, etc. See doctest examples below. For executable
    names this won't make any changes.
    >>> remove_so_extension_from_lib_name('libboost_thread-mt.so.1.63.0')
    'libboost_thread-mt.1.63.0'
    >>> remove_so_extension_from_lib_name('librocksdb_debug.so.4.6')
    'librocksdb_debug.4.6'
    >>> remove_so_extension_from_lib_name('librpc_header_proto.so')
    'librpc_header_proto'
    >>> remove_so_extension_from_lib_name('yb-master')
    'yb-master'
    """
    name = re.sub('[.]so$', '', lib_name)
    return re.sub('[.]so[.]', '.', name)


class GraphNode:
    def __init__(self, path, digest=None, is_seed=False, is_executable=False):
        if not os.path.exists(path):
            raise IOError("Path '{}' does not exist".format(path))
        path = realpath(path)
        self.path = path
        self.deps = find_elf_dependencies(path)
        if digest:
            self.digest = digest
        else:
            self.digest = compute_file_digest(path)
        self.is_seed = is_seed
        self.is_executable = is_executable
        self.needed_by_digests = set()
        self.needed_by = []

    # Hashing and equality are determined based on the digest of the node.
    def __hash__(self):
        return hash(self.digest)

    def __eq__(self, other):
        assert self.digest is not None
        return self.digest == other.digest

    def __str__(self):
        return 'GraphNode({}, {})'.format(repr(self.path), repr(self.digest))

    def __repr__(self):
        return str(self)

    def add_reverse_dependency(self, needed_by):
        if needed_by.digest not in self.needed_by_digests:
            self.needed_by.append(needed_by)
            self.needed_by_digests.add(needed_by.digest)

    def basename(self):
        return path_basename(self.path)

    def lib_link_dir_name_prefix(self):
        """
        Get a prefix to be used for the directory name in "lib" that would countain symlinks to
                all the libraries this binary needs.
        """
        return remove_so_extension_from_lib_name(path_basename(self.path))

    def indirect_dependencies_via_dlopen(self):
        """
        Recursively collect all dependencies this node depends on indirectly that are loaded via
        dlopen. These dependencies have to be symlinked directly this node's RPATH directory.
        """
        already_visited = set()
        dlopen_deps = set()

        def depth_first_search(node):
            already_visited.add(node)
            for d in node.deps:
                if d.via_dlopen:
                    dlopen_deps.add(d)
                if d.target_node not in already_visited:
                    depth_first_search(d.target_node)

        depth_first_search(self)
        return dlopen_deps


def find_canonical_library_path(p):
    """
    A workaround so that we don't create duplicates of libraries, e.g. snappy/gflags. When we see
    a path like
      thirdparty/build/uninstrumented/snappy-1.1.0/lib/libsnappy.so.1.1.4
    we try to convert it to
      thirdparty/installed/uninstrumented/lib/libsnappy.so.1.1.4
    """
    m = re.match(r'^(.*)/build/([a-z]+)/[^/]+/lib/([^/]+)$', p)
    if m:
        prefix = m.group(1)
        instrumentation_type = m.group(2)
        lib_name = m.group(3)
        canonical_location = path_join(prefix, 'installed', instrumentation_type, 'lib', lib_name)
        # Unfortunately, we can't just use digests to compare libraries here, because we end up
        # creating two distinct versions of the same library by independently updating RPATH in both
        # places to point to different things (because the new RPATH is relative to the library
        # directory).
        original_size = os.path.getsize(p)
        if os.path.exists(canonical_location):
            canonical_size = os.path.getsize(canonical_location)
            if canonical_size == original_size:
                logging.debug("Automatically using canonical location '{}' instead of '{}'".format(
                    canonical_location, p))
                return canonical_location
            else:
                logging.warn(
                    "Not using canonical location '{}' (size {}) for '{}' (size {}): "
                    "different file size".format(canonical_location, canonical_size,
                                                 p, original_size))

        else:
            logging.debug("Canonical location '{}' for '{}' does not exist".format(
                canonical_location, p))
    return p


def create_rel_symlink(existing_file_path, link_path):
    if os.path.islink(link_path) and \
            realpath(os.path.readlink(link_path)) == realpath(existing_file_path):
        return  # Nothing to do.

    try:
        os.symlink(os.path.relpath(existing_file_path, path_dirname(link_path)), link_path)
    except OSError, e:
        logging.warning("Error while trying to create a symlink {} -> {}".format(
            link_path, existing_file_path))
        raise e


class LibraryPackager:
    """
    A utility for starting with a set of 'seed' executables, and walking the dependency tree of
    libraries to find all libraries that need to be packaged with the product. As part of this we
    also create a directory for each binary (an executable or a shared library) and put symlinks to
    exact versions of its dependencies there, and set the binary's RPATH to that directory.
    """

    def __init__(self, build_dir, seed_executable_patterns, dest_dir):
        self.build_dir = build_dir
        if not os.path.exists(build_dir):
            raise IOError("Build directory '{}' does not exist".format(build_dir))
        self.seed_executable_patterns = seed_executable_patterns
        self.dest_dir = dest_dir
        logging.debug(
            "Traversing the dependency graph of executables/libraries, starting "
            "with seed executable patterns: {}".format(", ".join(seed_executable_patterns)))
        self.nodes_by_digest = {}
        self.nodes_by_path = {}

    def get_node_by_path(self, file_path, **kwargs):
        file_path = find_canonical_library_path(file_path)

        file_path = realpath(file_path)
        node = self.nodes_by_path.get(file_path, None)
        if node:
            return node
        digest = compute_file_digest(file_path)
        node = self.nodes_by_digest.get(digest, None)
        if node:
            return node
        node = GraphNode(file_path, digest=digest, **kwargs)
        logging.debug("Adding a dependency graph node: {}".format(node))
        self.nodes_by_path[file_path] = node
        self.nodes_by_digest[digest] = node
        for dep in node.deps:
            if dep.target.startswith(self.dest_dir + '/'):
                raise RuntimeError(
                    "{} has a dependency on a library in the destination directory: {}".format(
                        node, dep))

        return node

    def find_existing_node_by_path(self, path):
        return self.nodes_by_path[realpath(path)]

    def package_binaries(self):
        """
        The main entry point to this class. Arranges binaries (executables and shared libraries),
        starting with the given set of "seed executables", in the destination directory so that
        the executables can find all of their dependencies.
        """

        # Breadth-first search queue.
        queue = deque()

        for seed_executable_glob in self.seed_executable_patterns:
            re_match = re.match(r'^build/latest/(.*)$', seed_executable_glob)
            if re_match:
                updated_glob = path_join(self.build_dir, re_match.group(1))
                logging.info(
                    "Automatically updating seed glob to be relative to build dir: {} -> {}".format(
                        seed_executable_glob, updated_glob))
                seed_executable_glob = updated_glob
            for seed_executable in glob.glob(seed_executable_glob):
                queue.append(self.get_node_by_path(seed_executable, is_executable=True))

        # Also package Linuxbrew's dynamic linker.
        ld_path = realpath(path_join(LINUXBREW_HOME, 'lib', 'ld.so'))
        queue.append(self.get_node_by_path(ld_path))
        queue.append(self.get_node_by_path(PATCHELF_PATH, is_executable=True))

        # This will be a set of GraphNode objects. GraphNode should have proper hashing and equality
        # overrides.
        visited = set()

        while len(queue) > 0:
            node = queue.popleft()
            if node not in visited:
                visited.add(node)
                for new_dep in node.deps:
                    target_node = self.get_node_by_path(new_dep.target)
                    new_dep.target_node = target_node
                    if target_node not in visited:
                        queue.append(target_node)
                        target_node.add_reverse_dependency(node)

        nodes = self.nodes_by_digest.values()

        unique_dir_names = set()
        need_short_digest = False
        for node in nodes:
            name = node.lib_link_dir_name_prefix()
            if name in unique_dir_names:
                logging.warn(
                        "Duplicate library dir name: '{}', will use SHA256 digest prefix to "
                        "ensure directory name uniqueness".format(name))
                need_short_digest = True
            unique_dir_names.add(name)

        mkpath(path_join(self.dest_dir, 'bin'))
        dest_lib_dir = path_join(self.dest_dir, 'lib')
        dest_bin_dir = path_join(self.dest_dir, 'bin')
        for node in nodes:
            target_dir_basename = node.lib_link_dir_name_prefix()
            if need_short_digest:
                # We need to add a digest to the directory name to disambiguate between libraries
                # with the same name/version, e.g. two versions of libz 1.2.8 that come in when
                # building using Clang. Eventually we need to deduplicate libraries and remove the
                # need of doing this.
                target_dir_basename += '_' + node.digest[:8]
            node.target_dir = path_join(dest_lib_dir, target_dir_basename)
            node_basename = path_basename(node.path)
            if node.is_executable:
                node.target_path = path_join(dest_bin_dir, node_basename)
            else:
                node.target_path = path_join(node.target_dir, node_basename)
            mkpath(node.target_dir)
            if os.path.exists(node.target_path):
                os.unlink(node.target_path)
            shutil.copyfile(node.path, node.target_path)
            shutil.copymode(node.path, node.target_path)
            # Make the file writable so we can change the rpath in the file.
            os.chmod(node.target_path, os.stat(node.target_path).st_mode | stat.S_IWUSR)

        # Symlink the dynamic linker into an easy-to-find location inside lib.
        ld_node = self.find_existing_node_by_path(ld_path)
        ld_symlink_path = path_join(dest_lib_dir, 'ld.so')
        create_rel_symlink(ld_node.target_path, ld_symlink_path)

        patchelf_node = self.find_existing_node_by_path(PATCHELF_PATH)

        for node in nodes:
            if node in [ld_node, patchelf_node]:
                continue

            # Create symlinks in each node's directory that point to all of its dependencies.
            for dep in sorted(set(node.deps).union(set(node.indirect_dependencies_via_dlopen()))):
                existing_file_path = dep.target_node.target_path
                link_path = path_join(node.target_dir, os.path.basename(dep.name))
                if os.path.islink(link_path):
                    os.unlink(link_path)
                if realpath(existing_file_path) != realpath(link_path):
                    create_rel_symlink(existing_file_path, link_path)

            # Update RPATH of this binary to point to the directory that has symlinks to all the
            # right versions libraries it needs.
            new_rpath = '$ORIGIN'
            # Compute the path of the "target directory" (the directory that RPATH needs to
            # point to) relative to the binary path. If the binary is an executable, it won't be
            # that directory, because it will be in "bin" instead.
            target_dir_rel_path = os.path.relpath(
                    node.target_dir, path_dirname(node.target_path))
            if target_dir_rel_path != '.':
                if target_dir_rel_path.startswith('./'):
                    target_dir_rel_path = target_dir_rel_path[2:]
                new_rpath += '/' + target_dir_rel_path

            logging.debug("Setting RPATH of '{}' to '{}'".format(node.target_path, new_rpath))

            patchelf_result = run_patchelf('--set-rpath', new_rpath, node.target_path)
            if node.is_executable and \
               patchelf_result.stderr == PATCHELF_NOT_AN_ELF_EXECUTABLE:
                logging.info("Not an ELF executable: '{}', removing the directory '{}'.".format(
                    node.path, node.target_dir))
                shutil.rmtree(node.target_dir)
                continue

            node_metadata = dict(
               original_location=normalize_path_for_metadata(node.path),
               dependencies=[
                   d.as_metadata() for d in
                   sorted(list(node.deps), key=lambda dep: dep.target)])

            with open(path_join(node.target_dir, 'metadata.yml'), 'w') as metadata_file:
                yaml.dump(node_metadata, metadata_file)

            # Use the Linuxbrew dynamic linker for all files.
            run_patchelf('--set-interpreter', ld_symlink_path, node.target_path)

        # Create symlinks from the "lib" directory to the "libdb" library. We add the "lib"
        # directory to LD_LIBRARY_PATH, and this makes sure dlopen finds correct library
        # dependencies when it is invoked by libsasl2.
        for node in nodes:
            if node.basename().startswith('libdb-'):
                create_rel_symlink(node.target_path, path_join(dest_lib_dir, node.basename()))
        logging.info("Successfully generated a YB distribution at {}".format(self.dest_dir))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=LibraryPackager.__doc__)
    parser.add_argument('--build-dir',
                        help='Build directory to pick up executables/libraries from.',
                        required=True)
    parser.add_argument('--dest-dir',
                        help='Destination directory to save the self-sufficient directory tree '
                             'of executables and libraries at.',
                        required=True)
    parser.add_argument('--verbose',
                        help='Enable verbose output.',
                        action='store_true')
    args = parser.parse_args()
    log_level = logging.INFO
    if args.verbose:
        log_level = logging.DEBUG
    logging.basicConfig(
        level=log_level,
        format="[" + path_basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    release_manifest_path = path_join(YB_SRC_ROOT, 'yb_release_manifest.json')
    release_manifest = json.load(open(release_manifest_path))
    packager = LibraryPackager(build_dir=args.build_dir,
                               seed_executable_patterns=release_manifest['bin'],
                               dest_dir=args.dest_dir)
    packager.package_binaries()
