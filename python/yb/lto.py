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

from typing import Iterable, Set, List, Optional, Tuple
from yb.dep_graph_common import (
    DependencyGraph,
    Node,
    NodeType,
    DYLIB_SUFFIX,
)

from yb.common_util import (
    read_file,
    write_file,
    shlex_join,
)
from yb.build_paths import BuildPaths

from yugabyte_pycommon import WorkDirContext  # type: ignore

from pathlib import Path


SKIPPED_ARGS: Set[str] = {
    '-fPIC',
    '-lm',
    '-lrt',
    '-ldl',
    '-latomic',
    '-lcrypt',
    '-pthread',
    '-flto=thin',
    '-flto=full',
    '-Werror',
    '-Wall',
}


def get_static_lib_paths(thirdparty_path: str) -> List[str]:
    static_lib_paths = []
    for thirdparty_subdir in ['common', 'uninstrumented']:
        for root, dirs, files in os.walk(
                os.path.join(thirdparty_path, 'installed', thirdparty_subdir)):
            for file_name in files:
                if file_name.endswith('.a'):
                    static_lib_path = os.path.join(root, file_name)
                    static_lib_paths.append(static_lib_path)
    if not static_lib_paths:
        raise ValueError(
            "Did not find any static libraries at third-party path %s" % thirdparty_path)
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
    args: List[str]

    def __init__(self, initial_args: List[str] = []) -> None:
        self.args = list(initial_args)

    def append(self, arg: str) -> None:
        self.args.append(arg)

    def extend(self, more_args: List[str]) -> None:
        self.args.extend(more_args)

    def add_new_arg(self, new_arg: str) -> None:
        should_skip = (
            new_arg in SKIPPED_ARGS or
            new_arg.startswith('--gcc-toolchain')
        )

        if should_skip:
            logging.info("Skipping %s", new_arg)
            return

        self.append(new_arg)

    def contains(self, arg: str) -> bool:
        return arg in self.args


def process_original_link_cmd(original_link_cmd: List[str]) -> List[str]:
    """
    Remove unneeded parts of the original linker command.
    """
    if len(original_link_cmd) < 6:
        raise ValueError("Original linker command is too short: %s" % shlex_join(original_link_cmd))
    assert original_link_cmd[0] == ':'
    assert original_link_cmd[1] == '&&'
    assert original_link_cmd[2].endswith('/compiler-wrappers/c++')
    assert original_link_cmd[-2] == '&&'
    assert original_link_cmd[-1] == ':'

    return original_link_cmd[3:-2]


def is_yb_library(rel_path: str) -> bool:
    return rel_path.startswith(('lib/', 'postgres/lib/')) and rel_path.endswith(DYLIB_SUFFIX)


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

    # Build directory of the Postgres backend.
    pg_backend_build_dir: str

    # The command for linking the yb_pgbackend library.
    yb_pgbackend_link_cmd: List[str]

    lto_output_suffix: Optional[str]

    # Populated by consume_original_link_cmd.
    final_output_name: str

    def __init__(
            self,
            dep_graph: DependencyGraph,
            initial_node: Node,
            lto_output_suffix: Optional[str]) -> None:
        self.dep_graph = dep_graph
        self.initial_node = initial_node

        self.build_root = self.dep_graph.conf.build_root
        self.build_paths = BuildPaths(self.build_root)

        self.llvm_path = self.build_paths.get_llvm_path()
        self.thirdparty_path = self.build_paths.get_thirdparty_path()
        self.clang_cpp_path = self.build_paths.get_llvm_tool_path('clang++')

        assert initial_node.link_cmd
        self.original_link_args = process_original_link_cmd(initial_node.link_cmd)
        self.static_lib_paths = get_static_lib_paths(self.thirdparty_path)
        self.new_args = LinkCommand([self.clang_cpp_path])
        self.obj_file_graph_nodes = set()
        self.pg_backend_build_dir, self.yb_pgbackend_link_cmd = get_yb_pgbackend_link_cmd(
            self.build_root)

        self.lto_output_suffix = lto_output_suffix

    def convert_to_static_lib(self, arg: str) -> Optional[str]:
        """
        Given an argument to the original linker command, try to interpret it as a library, either
        specified as a shared library path, or using -l... syntax, and return the corresponding
        static library path if available.
        """
        if arg.startswith('/') and arg.endswith('.so'):
            arg_static_prefix = arg[:-3]

            static_found = False
            for suffix in ['.a', '-s.a']:
                arg_static = arg_static_prefix + suffix
                if os.path.exists(arg_static):
                    logging.info("Using static library %s instead of shared library %s",
                                 arg_static, arg)
                    return arg_static
            logging.info("Did not find static library corresponding to %s", arg)

        if arg.startswith('-l'):
            static_found = False
            logging.info("Looking for static lib for: %s", arg)
            lib_name = arg[2:]
            for static_lib_path in self.static_lib_paths:
                static_lib_basename = os.path.basename(static_lib_path)
                if (static_lib_basename == 'lib' + lib_name + '.a' or
                        static_lib_basename == 'lib' + lib_name + '-s.a'):
                    logging.info("Found static lib for %s: %s", lib_name, static_lib_path)
                    return static_lib_path
            logging.info("Did not find a static lib for %s", lib_name)

        if arg.endswith('.so') or '.so.' in arg:
            logging.info("Still using a shared library: %s", arg)

        return None

    def process_arg(self, arg: str) -> None:
        if arg in SKIPPED_ARGS:
            logging.info("Skipping argument: %s", arg)
            return

        new_arg = self.convert_to_static_lib(arg)
        if new_arg:
            if not self.new_args.contains(new_arg):
                self.new_args.add_new_arg(new_arg)
        else:
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
                    continue

                if arg.endswith('.cc.o'):
                    # E.g. tablet_server_main.cc.o.
                    # Remember this node for later deduplication.
                    self.obj_file_graph_nodes.add(self.dep_graph.find_node(os.path.realpath(arg)))

                self.process_arg(arg)

            if not output_name:
                raise ValueError("Did not find an output name in the original link command")
            self.final_output_name = os.path.abspath(output_name)
            logging.info("Final output file name: %s", self.final_output_name)
            if self.lto_output_suffix is not None:
                self.final_output_name += self.lto_output_suffix
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

                if (node.node_type == NodeType.OBJECT and
                        os.path.basename(os.path.dirname(node.path)) != 'yb_common_base.dir'):
                    self.new_args.add_new_arg(node.path)

            for arg in self.yb_pgbackend_link_cmd:
                if arg.endswith('.o'):
                    if os.path.basename(arg) == 'main_cpp_wrapper.cc.o':
                        # TOOD: why is this file even linked into libyb_pgbackend?
                        continue
                    self.new_args.append(os.path.join(self.pg_backend_build_dir, arg))
                    continue
                if (arg.startswith('-l') and
                        not self.new_args.contains(arg) and
                        not arg.startswith('-lyb_')):
                    self.process_arg(arg)

    def add_final_args(self) -> None:
        self.new_args.extend([
            '-L%s' % os.path.join(self.build_root, 'postgres', 'lib'),
            '-l:libpgcommon.a',
            '-l:libpgport.a',
            '-l:libpq.a',
            '-fwhole-program',
            '-Wl,-v',
            '-nostdlib++',
            '-flto=full',
        ])

        for lib_name in ['libc++.a', 'libc++abi.a']:
            self.new_args.append(os.path.join(
                self.thirdparty_path, 'installed', 'uninstrumented', 'libcxx', 'lib', lib_name))

        with WorkDirContext(self.build_root):
            self.write_link_cmd_file(self.final_output_name + '_lto_link_cmd_args.txt')

    def run_linker(self) -> None:
        with WorkDirContext(self.build_root):
            start_time_sec = time.time()
            logging.info("Running linker")
            try:
                subprocess.check_call(self.new_args.args)
            except subprocess.CalledProcessError as ex:
                # Avoid printing the extremely long command line.
                logging.error("Linker returned exit code %d", ex.returncode)
            elapsed_time_sec = time.time() - start_time_sec
            logging.info("Linking finished in %.1f sec", elapsed_time_sec)

    def write_link_cmd_file(self, out_path: str) -> None:
        logging.info("Writing the linker command line (one argument per line) to %s",
                     os.path.abspath(out_path))
        write_file('\n'.join(self.new_args.args), out_path)


def link_whole_program(
        dep_graph: DependencyGraph,
        initial_nodes: Iterable[Node],
        link_cmd_out_file: Optional[str],
        run_linker: bool,
        lto_output_suffix: Optional[str]) -> None:
    initial_node_list = list(initial_nodes)
    assert len(initial_node_list) == 1
    initial_node = initial_node_list[0]

    link_helper = LinkHelper(
        dep_graph=dep_graph,
        initial_node=initial_node,
        lto_output_suffix=lto_output_suffix
    )

    # We stop recursive traversal at executables because those are just code generators
    # (protoc-gen-insertions, protoc-gen-yrpc, bfql_codegen, bfpg_codegen).

    conf = dep_graph.conf

    new_args = link_helper.new_args

    link_helper.consume_original_link_cmd()
    link_helper.add_leaf_object_files()
    link_helper.add_final_args()

    with WorkDirContext(conf.build_root):
        if link_cmd_out_file:
            link_helper.write_link_cmd_file(link_cmd_out_file)
        if not run_linker:
            return
        link_helper.run_linker()
