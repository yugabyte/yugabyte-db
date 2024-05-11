# Copyright (c) Yugabyte, Inc.
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

import os
import time
import subprocess
import logging
import re
import psutil  # type: ignore

from typing import Iterable, Set, List, Optional, Tuple, Dict
from yugabyte.dep_graph_common import (
    DependencyGraph,
    Node,
    NodeType,
    DYLIB_SUFFIX,
)

from yugabyte.common_util import shlex_join, create_symlink
from yugabyte.file_util import read_file, write_file
from yugabyte.build_paths import BuildPaths

from yugabyte_pycommon import WorkDirContext  # type: ignore

from pathlib import Path

THIRDPARTY_INSTALLED_DIR_CMAKE_VAR_PREFIX = 'YB_THIRDPARTY_INSTALLED_DIR:STRING='


SKIPPED_ARGS: Set[str] = {
    '-fPIC',
    '-lm',
    '-ldl',
    '-latomic',
    '-lcrypt',
    '-pthread',
    '-flto=thin',
    '-flto=full',
    '-Werror',
    '-Wall',
}

LDD_OUTPUT_LINE_RE = re.compile(r'^(\S+) => (\S+) .*$')

SHARED_LIB_SUFFIX_RE = re.compile(r'^(.*)([.]so(?:[.]\d+)*)$')

# This is necessary to remove the -2.4 suffix to transform "liblber-2.4.so.2" to "liblber.a".
DASH_VERSION_SUFFIX_RE = re.compile(r'^(.*)-\d+(?:[.]\d+)*$')

LIBCXX_STATIC_LIB_NAMES = ('libc++.a', 'libc++abi.a')

SYSTEM_LIBRARY_PREFIXES = ('/lib/', '/lib64/', '/usr/')

STATIC_LIBRARY_SUFFIXES = ('.a', '-s.a')


def is_cmd_line_option(arg: str) -> bool:
    return arg.startswith('-')


def is_system_lib(lib_path: str) -> bool:
    return not is_cmd_line_option(lib_path) and lib_path.startswith(SYSTEM_LIBRARY_PREFIXES)


def is_static_lib(lib_path: str) -> bool:
    return not is_cmd_line_option(lib_path) and lib_path.endswith('.a')


def is_shared_lib(lib_path: str) -> bool:
    if is_cmd_line_option(lib_path):
        return False
    name = os.path.basename(lib_path)
    return '.so.' in name or name.endswith('.so')


def should_skip_argument(arg: str) -> bool:
    return arg in SKIPPED_ARGS or arg.startswith('--gcc-toolchain')


def get_static_lib_paths(thirdparty_installed_dir: str) -> List[str]:
    static_lib_paths = []
    candidate_dirs = []
    for thirdparty_subdir in ['common', 'uninstrumented']:
        candidate_dir = os.path.join(thirdparty_installed_dir, thirdparty_subdir)
        candidate_dirs.append(candidate_dir)
        for root, dirs, files in os.walk(candidate_dir):
            for file_name in files:
                if is_static_lib(file_name):
                    static_lib_path = os.path.join(root, file_name)
                    static_lib_paths.append(static_lib_path)
    if not static_lib_paths:
        for candidate_dir in candidate_dirs:
            logging.warning("Considered candidate directory: %s" % candidate_dir)
        raise ValueError(
            "Did not find any static libraries in third-party installed directory %s" %
            thirdparty_installed_dir)
    return static_lib_paths


def get_yb_pgbackend_link_cmd(build_root: str) -> Tuple[str, List[str]]:
    """
    Returns a tuple containing the Postgres backend build directory and a list representation
    of the linker command line for the yb_pgbackend library.
    """
    pg_backend_build_dir = os.path.join(build_root, 'postgres_build', 'src', 'backend')
    if not os.path.exists(pg_backend_build_dir):
        raise IOError("Directory does not exist: %s" % pg_backend_build_dir)

    prefix_str = 'link_cmd_libyb_pgbackend.so.'
    matched_files = [
        os.fspath(p) for p in Path(pg_backend_build_dir).glob('%s*' % prefix_str)
    ]
    if len(matched_files) != 1:
        raise ValueError(
            "Looking for the build command for the yb_pgbackend library, failed to find exactly "
            "one file starting with %s in %s. Got %s" % (
                prefix_str, pg_backend_build_dir, matched_files))
    return pg_backend_build_dir, read_file(matched_files[0]).strip().split()


class LinkCommand:
    """
    A wrapper around an array to manipulate a linker command line.
    """
    _args: List[str]
    _arg_set: Set[str]

    def __init__(self, initial_args: List[str] = []) -> None:
        self._args = list(initial_args)
        self._arg_set = set(initial_args)

    def append(self, arg: str) -> None:
        self._args.append(arg)
        self._arg_set.add(arg)

    def extend(self, more_args: List[str]) -> None:
        for arg in more_args:
            self.append(arg)

    def add_new_arg(self, new_arg: str) -> None:
        if self.contains(new_arg):
            return
        should_skip = should_skip_argument(new_arg)

        if should_skip:
            logging.info("Skipping %s", new_arg)
            return

        self.append(new_arg)

    def contains(self, arg: str) -> bool:
        return arg in self._arg_set

    def as_list(self) -> List[str]:
        return list(self._args)


def process_original_link_cmd(original_link_cmd: List[str]) -> List[str]:
    """
    Remove unneeded parts of the original linker command.
    """
    if len(original_link_cmd) < 6:
        raise ValueError("Original linker command is too short: %s" % shlex_join(original_link_cmd))
    assert original_link_cmd[0] == ':'
    assert original_link_cmd[1] == '&&'
    assert original_link_cmd[2].endswith('/compiler-wrappers/c++')
    end_index = original_link_cmd[2:].index('&&') + 2
    assert end_index > 3
    assert end_index < len(original_link_cmd)

    return original_link_cmd[3:end_index]


def is_yb_library(rel_path: str) -> bool:
    return rel_path.startswith(('lib/', 'postgres/lib/')) and rel_path.endswith(DYLIB_SUFFIX)


def split_shared_lib_ext(shared_lib_path: str) -> Tuple[Optional[str], Optional[str]]:
    """

    >>> split_shared_lib_ext('libkrb5.so.3')
    ('libkrb5', '.so.3')
    """
    m = SHARED_LIB_SUFFIX_RE.match(shared_lib_path)
    if m:
        return m.group(1), m.group(2)
    return None, None


def find_shared_lib_from_static(static_lib_path: str) -> str:
    assert is_static_lib(static_lib_path)
    shared_lib_path = static_lib_path[:-2] + '.so'
    if not os.path.exists(shared_lib_path):
        logging.warn("Shared library path does not exist: %s", shared_lib_path)
    return shared_lib_path


def remove_dash_numeric_suffix(lib_prefix: str) -> Optional[str]:
    """
    >>> remove_dash_numeric_suffix('liblber-2.4')
    'liblber'
    """
    m = DASH_VERSION_SUFFIX_RE.match(lib_prefix)
    if m:
        return m.group(1)
    return None


class LinkHelper:
    """
    Helps with creating and running a custom linking command for YugabyteDB outside of the build
    system.
    """
    dep_graph: DependencyGraph
    initial_node: Node

    # Arguments to the linker in the original command that produces the initial_node target.
    # Does not include the compiler driver executable.
    original_link_args: List[str]

    build_root: str
    build_paths: BuildPaths
    llvm_path: str
    thirdparty_path: str
    clang_cpp_path: str

    # Dependency graph nodes corresponding to the object files present in the original linker
    # command. Used for deduplication.
    obj_file_graph_nodes: Set[Node]

    new_args: LinkCommand

    lto_output_suffix: Optional[str]
    lto_output_path: Optional[str]

    # Populated by consume_original_link_cmd.
    final_output_name: str

    # We look at shared library dependencies (detected using ldd) of the libraries we add, and for
    # those dependencies that fall within the third-party directory, we determine the corresponding
    # static libraries and add them to the list below so we can link with them. This is necessary
    # because in some cases, e.g. for libgssapi_krb5, the static libraries we need to add cannot be
    # determined in any other way. The dictionary below maps the static library to the list of
    # shared libraries that necessitated its addition.
    static_libs_from_ldd: Dict[str, Set[str]]

    # Set of shared library file paths for which we have already examined dependencies using ldd
    # as described above.
    processed_shared_lib_deps_for: Set[str]

    thirdparty_installed_dir: str

    # Whether the original linking command includes the libyb_pgbackend library.
    yb_pgbackend_needed: bool

    def __init__(
            self,
            dep_graph: DependencyGraph,
            initial_node: Node,
            lto_output_suffix: Optional[str],
            lto_output_path: Optional[str]) -> None:
        self.dep_graph = dep_graph
        self.initial_node = initial_node

        self.build_root = self.dep_graph.conf.build_root
        self.build_paths = BuildPaths(self.build_root)

        self.llvm_path = self.build_paths.get_llvm_path()
        self.thirdparty_path = self.build_paths.get_thirdparty_path()
        self.clang_cpp_path = self.build_paths.get_llvm_tool_path('clang++')

        assert initial_node.link_cmd
        self.original_link_args = process_original_link_cmd(initial_node.link_cmd)
        self.new_args = LinkCommand([self.clang_cpp_path])
        self.obj_file_graph_nodes = set()

        self.lto_output_suffix = lto_output_suffix
        self.lto_output_path = lto_output_path

        self.static_libs_from_ldd = {}
        self.processed_shared_lib_deps_for = set()

        self.thirdparty_installed_dir = ''
        with open(os.path.join(self.build_root, 'CMakeCache.txt')) as cmake_cache_file:
            for line in cmake_cache_file:
                line = line.strip()
                if line.startswith(THIRDPARTY_INSTALLED_DIR_CMAKE_VAR_PREFIX):
                    self.thirdparty_installed_dir = line[
                            len(THIRDPARTY_INSTALLED_DIR_CMAKE_VAR_PREFIX):]
        if not self.thirdparty_installed_dir:
            self.thirdparty_installed_dir = os.path.join(self.thirdparty_path, 'installed')
        self.static_lib_paths = get_static_lib_paths(self.thirdparty_installed_dir)
        self.yb_pgbackend_needed = False

    def convert_to_static_lib(self, arg: str) -> Optional[str]:
        """
        Given an argument to the original linker command, try to interpret it as a library, either
        specified as a shared library path, or using -l... syntax, and return the corresponding
        static library path if available.
        """
        assert not is_system_lib(arg)

        if os.path.isabs(arg):
            lib_path_prefix, shared_lib_suffix = split_shared_lib_ext(arg)
            if lib_path_prefix is not None:
                static_found = False
                lib_path_prefixes: List[str] = [
                    item for item in [lib_path_prefix, remove_dash_numeric_suffix(lib_path_prefix)]
                    if item is not None
                ]
                static_lib_candidates = [
                    lib_path_prefix + suffix
                    for lib_path_prefix in lib_path_prefixes
                    for suffix in STATIC_LIBRARY_SUFFIXES
                ]

                for static_lib_path in static_lib_candidates:
                    if os.path.exists(static_lib_path):
                        logging.info("Using static library %s instead of shared library %s",
                                     static_lib_path, arg)
                        return static_lib_path
                raise ValueError("Did not find static library corresponding to %s" % arg)

        if arg.startswith('-l'):
            static_found = False
            logging.info("Looking for static lib for: %s", arg)
            lib_name = arg[2:]
            for static_lib_path in self.static_lib_paths:
                static_lib_basename = os.path.basename(static_lib_path)
                if any(static_lib_basename == 'lib' + lib_name + suffix
                       for suffix in STATIC_LIBRARY_SUFFIXES):
                    logging.info("Found static lib for %s: %s", lib_name, static_lib_path)
                    self.add_shared_library_dependencies(
                        find_shared_lib_from_static(static_lib_path))
                    return static_lib_path
            logging.info("Did not find a static lib for %s", lib_name)

        if is_shared_lib(arg):
            raise ValueError("Still using a shared library: %s" % arg)

        return None

    def add_shared_library_dependencies(self, shared_library_path: str) -> None:
        if shared_library_path in self.processed_shared_lib_deps_for:
            return

        self.processed_shared_lib_deps_for.add(shared_library_path)
        if not os.path.exists(shared_library_path):
            logging.info("File does not exist, not running ldd: %s", shared_library_path)
            return

        with open(shared_library_path, 'rb') as shared_library_file:
            first_bytes = shared_library_file.read(6)
        if first_bytes == b'INPUT(':
            # Deal with the following contents of the "shared library" file libc++.so.
            #
            # INPUT(libc++.so.1 -lc++abi)
            #
            # We will recurse into each of the mentioned files.
            with open(shared_library_path) as shared_library_text_file:
                shared_library_str = shared_library_text_file.read().strip()
            logging.info(f"Parsing text content of {shared_library_path}: {shared_library_str}")
            assert (shared_library_str.startswith('INPUT(') and
                    shared_library_str.endswith(')') and
                    '\n' not in shared_library_str), \
                f'Unexpected text contents of {shared_library_path}: {shared_library_str}'
            sub_components = shared_library_str[6:-1].split()
            shared_library_dir = os.path.dirname(shared_library_path)
            for sub_component in sub_components:
                if sub_component.startswith('-l'):
                    sub_component_file_name = sub_component[2:] + '.so'
                else:
                    sub_component_file_name = sub_component
                sub_path = os.path.join(shared_library_dir, sub_component_file_name)
                logging.info(f"Recursing into shared library path {sub_path}")
                self.add_shared_library_dependencies(sub_path)
            return

        ldd_output = subprocess.check_output(['ldd', shared_library_path]).decode('utf-8')
        for line in ldd_output.split('\n'):
            line = line.strip()
            ldd_output_line_match = LDD_OUTPUT_LINE_RE.match(line)
            if ldd_output_line_match:
                so_name = ldd_output_line_match.group(1)
                so_path = ldd_output_line_match.group(2)
                if so_path.startswith(self.thirdparty_path + '/'):
                    static_lib_path = self.convert_to_static_lib(so_path)
                    if static_lib_path:
                        if os.path.basename(static_lib_path) in LIBCXX_STATIC_LIB_NAMES:
                            # Skip libc++ and libc++abi, we will add them explicitly later.
                            # All third-party libraries written in C++ will depend on these and it
                            # is not very useful to include that in the output.
                            continue
                        if static_lib_path not in self.static_libs_from_ldd:
                            self.static_libs_from_ldd[static_lib_path] = set()
                        self.static_libs_from_ldd[static_lib_path].add(os.path.realpath(
                            shared_library_path))

    def process_arg(self, arg: str) -> None:
        assert arg is not None
        if arg in SKIPPED_ARGS:
            logging.info("Skipping argument: %s", arg)
            return

        if is_system_lib(arg):
            if is_static_lib(arg):
                raise ValueError("Linking with a system static library is not allowed: %s" % arg)
            if is_shared_lib(arg):
                name = os.path.basename(arg)
                if name == 'librt.so':
                    arg = '-lrt'
                else:
                    raise ValueError("System shared library: %s" % arg)
        else:
            if is_shared_lib(arg):
                self.add_shared_library_dependencies(arg)

            arg = self.convert_to_static_lib(arg) or arg

        self.new_args.add_new_arg(arg)

    def consume_original_link_cmd(self) -> None:
        """
        Goes over the original linker command and reuses some of its arguments for the new command.
        """
        with WorkDirContext(self.build_root):
            expect_output_name = False
            output_name: Optional[str] = None
            for arg in self.original_link_args:
                if arg == '-o':
                    expect_output_name = True
                    continue
                if expect_output_name:
                    if output_name:
                        raise ValueError(
                            "Found multiple output names in the original link command: "
                            "%s and %s" % (output_name, arg))
                    output_name = arg
                    expect_output_name = False
                    continue
                expect_output_name = False

                if is_yb_library(arg):
                    logging.info("Skipping YB library: %s", arg)
                    if os.path.splitext(os.path.basename(arg))[0] == 'libyb_pgbackend':
                        logging.info("Recording the need for the yb_pgbackend library")
                        self.yb_pgbackend_needed = True
                    continue

                if arg.endswith('.cc.o'):
                    # E.g. tablet_server_main.cc.o.
                    # Remember this node for later deduplication.
                    self.obj_file_graph_nodes.add(self.dep_graph.find_node(os.path.realpath(arg)))

                self.process_arg(arg)

            if not output_name:
                raise ValueError("Did not find an output name in the original link command")
            self.final_output_name = os.path.abspath(output_name)
            if self.lto_output_path is not None:
                self.final_output_name = self.lto_output_path
            elif self.lto_output_suffix is not None:
                self.final_output_name += self.lto_output_suffix
            logging.info(f"Output file name from the original link command: {output_name}")
            logging.info(f"Final output file path: {self.final_output_name}")
            self.new_args.extend(['-o', self.final_output_name])

    def add_leaf_object_files(self) -> None:
        """
        Goes over all the object files that the original node transitively depends on, and adds
        them to the link command if they have not already been added.
        """

        transitive_deps = self.initial_node.get_recursive_deps(
            skip_node_types=set([NodeType.EXECUTABLE]))
        with WorkDirContext(self.build_root):
            # Sort nodes by path for determinism.
            for node in sorted(list(transitive_deps), key=lambda dep: dep.path):
                if node in self.obj_file_graph_nodes:
                    # Dedup .cc.o files already existing on the command line.
                    continue

                if node.node_type == NodeType.OBJECT:
                    self.new_args.add_new_arg(node.path)

            if self.yb_pgbackend_needed:
                pg_backend_build_dir, yb_pgbackend_link_cmd = get_yb_pgbackend_link_cmd(
                    self.build_root)
                for arg in yb_pgbackend_link_cmd:
                    if arg.endswith('.o'):
                        if os.path.basename(arg) == 'main_cpp_wrapper.cc.o':
                            # TOOD: why is this file even linked into libyb_pgbackend?
                            continue
                        self.new_args.append(os.path.join(pg_backend_build_dir, arg))
                        continue
                    if (arg.startswith('-l') and
                            not self.new_args.contains(arg) and
                            not arg.startswith('-lyb_')):
                        self.process_arg(arg)

    def add_final_args(self, lto_type: str) -> None:
        assert lto_type in ['full', 'thin']
        for static_lib_path in sorted(self.static_libs_from_ldd):
            if not self.new_args.contains(static_lib_path):
                logging.info(
                    "Adding a static library determined using shared library dependencies: %s "
                    "(needed by: %s)",
                    static_lib_path,
                    # The static_libs_from_ldd dictionary stores the set of shared library paths
                    # that caused us to add a particular static library dependency as the value
                    # corresponding to that static library's path in the key.
                    ', '.join(sorted(self.static_libs_from_ldd[static_lib_path])))
                self.new_args.append(static_lib_path)

        self.new_args.append('-fwhole-program')
        if self.yb_pgbackend_needed:
            # We are most likely building the main server process, so add other Postgres libraries
            # as well.
            self.new_args.extend([
                '-L%s' % os.path.join(self.build_root, 'postgres', 'lib'),
                '-l:libpgcommon.a',
                '-l:libpgport.a',
                '-l:libpq.a'
            ])

        self.new_args.extend([
            '-Wl,-v',
            '-nostdlib++',
            # For __res_nsearch, ns_initparse, ns_parserr, ns_name_uncompress.
            # See https://github.com/yugabyte/yugabyte-db/issues/12738 for details.
            '-lresolv',
            '-flto=' + lto_type,
        ])

        for lib_name in LIBCXX_STATIC_LIB_NAMES:
            self.new_args.append(os.path.join(
                self.thirdparty_installed_dir, 'uninstrumented', 'libcxx',
                'lib', lib_name))

        with WorkDirContext(self.build_root):
            self.write_link_cmd_file(self.final_output_name + '_lto_link_cmd_args.txt')

    def run_linker(self) -> None:
        with WorkDirContext(self.build_root):
            start_time_sec = time.time()
            logging.info("Running linker")
            exit_code = 0
            try:
                subprocess.check_call(self.new_args.as_list())
            except subprocess.CalledProcessError as ex:
                # Avoid printing the extremely long command line.
                logging.error("Linker returned exit code %d", ex.returncode)
                exit_code = ex.returncode
            elapsed_time_sec = time.time() - start_time_sec
            logging.info("Linking finished in %.1f sec", elapsed_time_sec)
            if exit_code != 0:
                raise RuntimeError("Linker returned exit code %d" % exit_code)

    def write_link_cmd_file(self, out_path: str) -> None:
        logging.info("Writing the linker command line (one argument per line) to %s",
                     os.path.abspath(out_path))
        write_file(content='\n'.join(self.new_args.as_list()),
                   output_file_path=out_path)


def wait_for_free_memory(
        required_mem_gib: float,
        timeout_minutes: float) -> None:
    BYTES_IN_GIB = 1024.0**3
    mem_info = psutil.virtual_memory()
    total_mem_gib = mem_info.total / BYTES_IN_GIB
    if total_mem_gib < required_mem_gib:
        raise RuntimeError(
            "The system only has %.3f GiB of memory but %.3f GiB are required for LTO liniking.",
            total_mem_gib, required_mem_gib)
    start_time_sec = time.time()
    iteration_number = 0
    timeout_sec = timeout_minutes * 60.0

    elapsed_time_sec: float = 0.0

    def get_available_gib() -> float:
        nonlocal mem_info
        return mem_info.available / BYTES_IN_GIB

    def get_progress_message() -> str:
        nonlocal elapsed_time_sec, required_mem_gib, mem_info
        return (
            "%.1f seconds to have at least %.1f GiB of RAM available, currently "
            "available: %.1f" % (
                elapsed_time_sec, required_mem_gib, get_available_gib()
            ))

    while True:
        mem_info = psutil.virtual_memory()
        elapsed_time_sec = time.time() - start_time_sec
        if get_available_gib() >= required_mem_gib:
            if iteration_number != 0:
                logging.info("Waited for " + get_progress_message())
            return
        iteration_number += 1
        if elapsed_time_sec > timeout_sec:
            raise RuntimeError(
                ("Hit timeout of %.1f seconds after waiting for " % timeout_sec) +
                get_progress_message())
        time.sleep(1)
        if iteration_number % 60 == 0:
            logging.info("Still waiting for " + get_progress_message())


def link_whole_program(
        dep_graph: DependencyGraph,
        initial_nodes: Iterable[Node],
        link_cmd_out_file: Optional[str],
        run_linker: bool,
        lto_output_suffix: Optional[str],
        lto_output_path: Optional[str],
        lto_type: str,
        symlink_as: Optional[List[str]]) -> None:
    if os.environ.get('YB_SKIP_FINAL_LTO_LINK') == '1':
        raise ValueError('YB_SKIP_FINAL_LTO_LINK is set, the final LTO linking step should not '
                         'have been invoked. Perhaps yb_build.sh should be invoked with '
                         'the --force-run-cmake argument.')

    wait_for_free_memory(required_mem_gib=13, timeout_minutes=20)

    initial_node_list = list(initial_nodes)
    if len(initial_node_list) != 1:
        initial_node_list_str = '\n    '.join([str(node) for node in initial_node_list])
        raise ValueError(
            f"Expected exactly one initial node to determine object files and static libraries "
            f"to link, got {len(initial_node_list)}:\n    {initial_node_list_str}")

    initial_node = initial_node_list[0]

    link_helper = LinkHelper(
        dep_graph=dep_graph,
        initial_node=initial_node,
        lto_output_suffix=lto_output_suffix,
        lto_output_path=lto_output_path
    )

    # We stop recursive traversal at executables because those are just code generators
    # (protoc-gen-insertions, protoc-gen-yrpc, bfql_codegen, bfpg_codegen).

    conf = dep_graph.conf

    link_helper.consume_original_link_cmd()
    link_helper.add_leaf_object_files()
    link_helper.add_final_args(lto_type=lto_type)

    with WorkDirContext(conf.build_root):
        if link_cmd_out_file:
            link_helper.write_link_cmd_file(link_cmd_out_file)
        if not run_linker:
            return
        link_helper.run_linker()

    if symlink_as is not None:
        for symlink_name in symlink_as:
            create_symlink(
                os.path.basename(link_helper.final_output_name),
                os.path.join(os.path.dirname(link_helper.final_output_name),
                             symlink_name))
