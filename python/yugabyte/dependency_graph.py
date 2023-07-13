#!/usr/bin/env python3

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


"""
Build a dependency graph of sources, object files, libraries, and binaries.  Compute the set of
tests that might be affected by changes to the given set of source files.
"""

import argparse
import json
import logging
import os
import subprocess
import sys
import unittest
import pipes
import time

from datetime import datetime
from enum import Enum
from typing import Optional, List, Dict, TypeVar, Set, Any, Iterable, cast
from pathlib import Path

from yugabyte.common_util import (
        arg_str_to_bool,
        get_bool_env_var,
        group_by,
        is_lto_build_root,
        make_set,
        shlex_join,
)
from yugabyte.command_util import mkdir_p
from yugabyte.source_files import (
    CATEGORIES_NOT_CAUSING_RERUN_OF_ALL_TESTS,
    get_file_category,
    SourceFileCategory,
)
from yugabyte.dep_graph_common import (
    CMakeDepGraph,
    DependencyGraph,
    DepGraphConf,
    DYLIB_SUFFIX,
    is_object_file,
    is_shared_library,
    Node,
    NodeType,
)
from yugabyte_pycommon import WorkDirContext  # type: ignore
from yugabyte.lto import link_whole_program


class Command(Enum):
    DEPS = 'deps'
    REVERSE_DEPS = 'rev-deps'
    AFFECTED = 'affected'
    SELF_TEST = 'self-test'
    DEBUG_DUMP = 'debug-dump'
    LINK_WHOLE_PROGRAM = 'link-whole-program'

    def __str__(self) -> str:
        return self.value

    def __lt__(self, other: Any) -> bool:
        assert isinstance(other, Command)
        return self.value < other.value

    def __eq__(self, other: Any) -> bool:
        assert isinstance(other, Command)
        return self.value == other.value


COMMANDS_NOT_NEEDING_TARGET_SET = [Command.SELF_TEST, Command.DEBUG_DUMP]


def set_to_str(items: Set[Any]) -> str:
    return ",\n".join(sorted(list(items)))


class DependencyGraphBuilder:
    """
    Builds a dependency graph from the contents of the build directory. Each node of the graph is
    a file (an executable, a dynamic library, or a source file).
    """
    conf: DepGraphConf
    compile_dirs: Set[str]
    compile_commands: Dict[str, Any]
    useful_base_dirs: Set[str]
    unresolvable_rel_paths: Set[str]
    resolved_rel_paths: Dict[str, str]
    dep_graph: 'DependencyGraph'
    cmake_dep_graph: CMakeDepGraph

    def __init__(self, conf: DepGraphConf) -> None:
        self.conf = conf
        self.compile_dirs = set()
        self.useful_base_dirs = set()
        self.unresolvable_rel_paths = set()
        self.resolved_rel_paths = {}
        self.dep_graph = DependencyGraph(conf)

    def load_cmake_deps(self) -> None:
        self.cmake_dep_graph = CMakeDepGraph(self.conf.build_root)

    def parse_link_and_depend_files_for_make(self) -> None:
        logging.info(
                "Parsing link.txt and depend.make files from the build tree at '{}'".format(
                    self.conf.build_root))
        start_time = datetime.now()

        num_parsed = 0
        for root, dirs, files in os.walk(self.conf.build_root):
            for file_name in files:
                file_path = os.path.join(root, file_name)
                if file_name == 'depend.make':
                    self.parse_depend_file(file_path)
                    num_parsed += 1
                elif file_name == 'link.txt':
                    self.parse_link_txt_file(file_path)
                    num_parsed += 1
            if num_parsed % 10 == 0:
                print('.', end='', file=sys.stderr)
                sys.stderr.flush()
        print('', file=sys.stderr)
        sys.stderr.flush()

        logging.info("Parsed link.txt and depend.make files in %.2f seconds" %
                     (datetime.now() - start_time).total_seconds())

    def find_proto_files(self) -> None:
        logging.info("Finding .proto files in the source tree at '{}'".format(
                self.conf.src_dir_path))
        source_str = 'proto files in {}'.format(self.conf.src_dir_path)
        for root, dirs, files in os.walk(self.conf.src_dir_path):
            for file_name in files:
                if file_name.endswith('.proto'):
                    self.dep_graph.find_or_create_node(
                            os.path.join(root, file_name),
                            source_str=source_str)

    def find_flex_bison_files(self) -> None:
        """
        CMake commands generally include the C file compilation, but misses the case where flex
        or bison generates those files, somewhat similiar to .proto files.
        Only examining src/yb tree.
        """
        src_yb_root = os.path.join(self.conf.src_dir_path, 'yb')
        logging.info("Finding .y and .l files in the source tree at '%s'", src_yb_root)
        source_str = 'flex and bison files in {}'.format(src_yb_root)
        for root, dirs, files in os.walk(src_yb_root):
            for file_name in files:
                if file_name.endswith('.y') or file_name.endswith('.l'):
                    rel_path = os.path.relpath(root, self.conf.yb_src_root)
                    assert not rel_path.startswith('../')
                    dependent_file = os.path.join(self.conf.build_root,
                                                  rel_path,
                                                  file_name[:len(file_name)] + '.cc')
                    self.register_dependency(dependent_file,
                                             os.path.join(root, file_name),
                                             source_str)

    def match_cmake_targets_with_files(self) -> None:
        logging.info("Matching CMake targets with the files found")
        self.cmake_target_to_nodes: Dict[str, Set[Node]] = {}
        for node in self.dep_graph.get_nodes():
            node_cmake_target = node.get_cmake_target()
            if node_cmake_target:
                node_set = self.cmake_target_to_nodes.get(node_cmake_target)
                if not node_set:
                    node_set = set()
                    self.cmake_target_to_nodes[node_cmake_target] = node_set
                node_set.add(node)

        self.cmake_target_to_node: Dict[str, Node] = {}
        unmatched_cmake_targets = set()
        cmake_dep_graph = self.dep_graph.get_cmake_dep_graph()
        for cmake_target in cmake_dep_graph.cmake_targets:
            nodes = self.cmake_target_to_nodes.get(cmake_target)
            if not nodes:
                unmatched_cmake_targets.add(cmake_target)
                continue

            if len(nodes) > 1:
                raise RuntimeError("Ambiguous nodes found for CMake target '{}': {}".format(
                    cmake_target, nodes))
            self.cmake_target_to_node[cmake_target] = list(nodes)[0]

        if unmatched_cmake_targets:
            logging.warning(
                    "These CMake targets do not have any associated files: %s",
                    sorted(unmatched_cmake_targets))

        # We're not adding nodes into our graph for CMake targets. Instead, we're finding files
        # that correspond to CMake targets, and add dependencies to those files.
        for cmake_target, cmake_target_deps in cmake_dep_graph.cmake_deps.items():
            if cmake_target in unmatched_cmake_targets:
                continue
            node = self.cmake_target_to_node[cmake_target]
            for cmake_target_dep in cmake_target_deps:
                if cmake_target_dep in unmatched_cmake_targets:
                    continue
                node.add_dependency(self.cmake_target_to_node[cmake_target_dep])

    def resolve_rel_path(self, rel_path: str) -> Optional[str]:
        if os.path.isabs(rel_path):
            return rel_path

        if rel_path in self.unresolvable_rel_paths:
            return None
        existing_resolution = self.resolved_rel_paths.get(rel_path)
        if existing_resolution:
            return existing_resolution

        candidates = set()
        for base_dir in self.conf.rel_path_base_dirs:
            candidate_path = os.path.abspath(os.path.join(base_dir, rel_path))
            if os.path.exists(candidate_path):
                self.useful_base_dirs.add(base_dir)
                candidates.add(candidate_path)
        if not candidates:
            self.unresolvable_rel_paths.add(rel_path)
            return None
        if len(candidates) > 1:
            logging.warning("Ambiguous ways to resolve '{}': '{}'".format(
                rel_path, set_to_str(candidates)))
            self.unresolvable_rel_paths.add(rel_path)
            return None

        resolved = list(candidates)[0]
        self.resolved_rel_paths[rel_path] = resolved
        return resolved

    def resolve_dependent_rel_path(self, rel_path: str) -> str:
        if os.path.isabs(rel_path):
            return rel_path
        if is_object_file(rel_path):
            return os.path.join(self.conf.build_root, rel_path)
        raise RuntimeError(
            "Don't know how to resolve relative path of a 'dependent': {}".format(
                rel_path))

    def parse_ninja_metadata(self) -> None:
        with WorkDirContext(self.conf.build_root):
            ninja_path = os.environ.get('YB_NINJA_PATH', 'ninja')
            logging.info("Ninja executable path: %s", ninja_path)
            logging.info("Running 'ninja -t commands'")
            subprocess.check_call('{} -t commands >ninja_commands.txt'.format(
                pipes.quote(ninja_path)), shell=True)
            logging.info("Parsing the output of 'ninja -t commands' for linker commands")
            start_time_sec = time.time()
            self.parse_link_txt_file('ninja_commands.txt')
            logging.info("Parsing linker commands took %.1f seconds", time.time() - start_time_sec)

            logging.info("Running 'ninja -t deps'")
            subprocess.check_call('{} -t deps >ninja_deps.txt'.format(
                pipes.quote(ninja_path)), shell=True)

            start_time_sec = time.time()
            logging.info("Parsing the output of 'ninja -t deps' to infer dependencies")
            self.parse_depend_file('ninja_deps.txt')
            logging.info("Parsing dependencies took %.1f seconds", time.time() - start_time_sec)

    def register_dependency(self,
                            dependent: str,
                            dependency: str,
                            dependency_came_from: str) -> None:
        dependent = self.resolve_dependent_rel_path(dependent.strip())
        dependency = dependency.strip()
        dependency_resolved: Optional[str] = self.resolve_rel_path(dependency)
        if dependency_resolved:
            dependent_node = self.dep_graph.find_or_create_node(
                    dependent, source_str=dependency_came_from)
            dependency_node = self.dep_graph.find_or_create_node(
                    dependency_resolved, source_str=dependency_came_from)
            dependent_node.add_dependency(dependency_node)

    def parse_depend_file(self, depend_make_path: str) -> None:
        """
        Parse either a depend.make file from a CMake-generated Unix Makefile project (fully built)
        or the output of "ninja -t deps" in a Ninja project.
        """
        with open(depend_make_path) as depend_file:
            dependent = None
            for line in depend_file:
                line_orig = line
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                if ': #' in line:
                    # This is a top-level line from "ninja -t deps".
                    dependent = line.split(': #')[0]
                elif line_orig.startswith(' ' * 4) and not line_orig.startswith(' ' * 5):
                    # This is a line listing a particular dependency from "ninja -t deps".
                    dependency = line
                    if not dependent:
                        raise ValueError("Error parsing %s: no dependent found for dependency %s" %
                                         (depend_make_path, dependency))
                    self.register_dependency(dependent, dependency, depend_make_path)
                elif ': ' in line:
                    # This is a line from depend.make. It has exactly one dependency on the right
                    # side.
                    dependent, dependency = line.split(':')
                    self.register_dependency(dependent, dependency, depend_make_path)
                else:
                    raise ValueError("Could not parse this line from %s:\n%s" %
                                     (depend_make_path, line_orig))

    def parse_link_txt_file(self, link_txt_path: str) -> None:
        with open(link_txt_path) as link_txt_file:
            for line in link_txt_file:
                line = line.strip()
                if line:
                    self.parse_link_command(line, link_txt_path)

    def parse_link_command(self, link_command: str, link_txt_path: str) -> None:
        link_args = link_command.split()
        output_path = None
        inputs = []
        if self.conf.is_ninja:
            base_dir = self.conf.build_root
        else:
            base_dir = os.path.join(os.path.dirname(link_txt_path), '..', '..')
        i = 0
        compilation = False
        while i < len(link_args):
            arg = link_args[i]
            if arg == '-o':
                new_output_path = link_args[i + 1]
                if output_path and new_output_path and output_path != new_output_path:
                    raise RuntimeError(
                        "Multiple output paths for a link command ('{}' and '{}'): {}".format(
                            output_path, new_output_path, link_command))
                output_path = new_output_path
                if self.conf.is_ninja and is_object_file(output_path):
                    compilation = True
                i += 1
            elif arg == '--shared_library_suffix':
                # Skip the next argument.
                i += 1
            elif not arg.startswith('@rpath/'):
                if is_object_file(arg):
                    node = self.dep_graph.find_or_create_node(
                            os.path.abspath(os.path.join(base_dir, arg)))
                    inputs.append(node.path)
                elif is_shared_library(arg):
                    node = self.dep_graph.find_or_create_node(
                            os.path.abspath(os.path.join(base_dir, arg)),
                            source_str=link_txt_path)
                    inputs.append(node.path)

            i += 1

        if self.conf.is_ninja and compilation:
            # Ignore compilation commands. We are interested in linking commands only at this point.
            return

        if not output_path:
            if self.conf.is_ninja:
                return
            raise RuntimeError("Could not find output path for link command: %s" % link_command)

        if not os.path.isabs(output_path):
            output_path = os.path.abspath(os.path.join(base_dir, output_path))
        output_node = self.dep_graph.find_or_create_node(output_path, source_str=link_txt_path)
        output_node.validate_existence()
        for input_node in inputs:
            dependency_node = self.dep_graph.find_or_create_node(input_node)
            if output_node is dependency_node:
                raise RuntimeError(
                    ("Cannot add a dependency from a node to itself: %s. "
                     "Parsed from command: %s") % (output_node, link_command))
            output_node.add_dependency(dependency_node)
        output_node.set_link_command(link_args)

    def build(self) -> 'DependencyGraph':
        compile_commands_path = os.path.join(self.conf.build_root, 'compile_commands.json')
        cmake_deps_path = os.path.join(self.conf.build_root, 'yb_cmake_deps.txt')
        run_build = False
        for file_path in [compile_commands_path, cmake_deps_path]:
            if not os.path.exists(file_path):
                logging.info(f"File {file_path} does not exist, will try to re-run the build.")
                run_build = True
        if run_build:
            if self.conf.never_run_build:
                logging.warning("--never-run-build specified, will NOT run the build.")
            else:
                # This is mostly useful during testing. We don't want to generate the list of
                # compilation commands by default because it takes a while, so only generate it on
                # demand.
                os.environ['YB_EXPORT_COMPILE_COMMANDS'] = '1'
                mkdir_p(self.conf.build_root)

                build_cmd = [
                    os.path.join(self.conf.yb_src_root, 'yb_build.sh'),
                    self.conf.build_type,
                    '--cmake-only',
                    '--no-rebuild-thirdparty',
                    '--build-root', self.conf.build_root
                ]
                if self.conf.build_args:
                    # This is not ideal because it does not give a way to specify arguments with
                    # embedded spaces but is OK for our needs here.
                    logging.info("Running the build: %s", shlex_join(build_cmd))
                    build_cmd.extend(self.conf.build_args.strip().split())

                subprocess.check_call(build_cmd)

        logging.info("Loading compile commands from '{}'".format(compile_commands_path))
        with open(compile_commands_path) as commands_file:
            self.compile_commands = json.load(commands_file)

        for entry in self.compile_commands:
            self.compile_dirs.add(cast(Dict[str, Any], entry)['directory'])

        if self.conf.is_ninja:
            self.parse_ninja_metadata()
        else:
            self.parse_link_and_depend_files_for_make()
        self.find_proto_files()
        self.find_flex_bison_files()
        self.dep_graph.validate_node_existence()

        self.load_cmake_deps()
        self.dep_graph.cmake_dep_graph = self.cmake_dep_graph
        self.match_cmake_targets_with_files()
        self.dep_graph._add_proto_generation_deps()

        return self.dep_graph


class DependencyGraphTest(unittest.TestCase):
    # This is a static field because we need to set it without a concrete test object.
    dep_graph: Optional[DependencyGraph] = None

    # Basename -> basenames affected by it.
    affected_basenames_cache: Dict[str, Set[str]] = {}

    @classmethod
    def set_dep_graph(self, dep_graph: DependencyGraph) -> None:
        DependencyGraphTest.dep_graph = dep_graph

    def get_dep_graph(self) -> DependencyGraph:
        dep_graph = DependencyGraphTest.dep_graph
        assert dep_graph is not None
        return dep_graph

    def get_build_root(self) -> str:
        return self.get_dep_graph().conf.build_root

    def is_lto(self) -> bool:
        return is_lto_build_root(self.get_build_root())

    def get_dynamic_exe_suffix(self) -> str:
        if self.is_lto():
            return '-dynamic'
        return ''

    def cxx_tests_are_being_built(self) -> bool:
        return self.get_dep_graph().cxx_tests_are_being_built()

    def get_yb_master_target(self) -> str:
        return 'yb-master' + self.get_dynamic_exe_suffix()

    def get_yb_tserver_target(self) -> str:
        return 'yb-tserver' + self.get_dynamic_exe_suffix()

    def get_affected_basenames(self, initial_basename: str) -> Set[str]:
        affected_basenames_from_cache: Optional[Set[str]] = self.affected_basenames_cache.get(
            initial_basename)
        if affected_basenames_from_cache is not None:
            return affected_basenames_from_cache

        dep_graph = self.get_dep_graph()
        affected_basenames_for_test: Set[str] = \
            dep_graph.affected_basenames_by_basename_for_test(initial_basename)
        self.affected_basenames_cache[initial_basename] = affected_basenames_for_test
        if dep_graph.conf.verbose:
            # This is useful to get inspiration for new tests.
            logging.info("Files affected by {}:\n    {}".format(
                initial_basename, "\n    ".join(sorted(affected_basenames_for_test))))
        return affected_basenames_for_test

    def assert_all_affected_by(
            self,
            expected_affected_basenames: List[str],
            initial_basename: str) -> None:
        """
        Asserts that all given files are affected by the given file. Other files might also be
        affected and that's OK.
        """
        affected_basenames = self.get_affected_basenames(initial_basename)
        remaining_basenames = make_set(expected_affected_basenames) - set(affected_basenames)
        if remaining_basenames:
            self.assertFalse(
                "Expected files {} to be affected by {}, but they were not".format(
                    sorted(remaining_basenames),
                    initial_basename))

    def assert_all_unaffected_by(
            self,
            unaffected_basenames: List[str],
            initial_basename: str) -> None:
        """
        Asserts that the given files are unaffected by the given file.
        """
        affected_basenames: Set[str] = self.get_affected_basenames(initial_basename)
        incorrectly_affected: Set[str] = make_set(unaffected_basenames) & affected_basenames
        if incorrectly_affected:
            self.assertFalse(
                    ("Expected files {} to be unaffected by {}, but they are. Other affected "
                     "files: {}").format(
                         sorted(incorrectly_affected),
                         initial_basename,
                         sorted(affected_basenames - incorrectly_affected)))

    def assert_exact_set_affected_by(
            self,
            expected_affected_basenames: List[str],
            initial_basename: str) -> None:
        """
        Checks the exact set of files affected by the given file.
        """
        affected_basenames = self.get_affected_basenames(initial_basename)
        self.assertEqual(make_set(expected_affected_basenames), affected_basenames)

    def test_master_main(self) -> None:
        self.assert_all_affected_by([
                'libintegration-tests' + DYLIB_SUFFIX,
                self.get_yb_master_target()
            ], 'master_main.cc')

        self.assert_all_unaffected_by(['yb-tserver'], 'master_main.cc')

    def test_tablet_server_main(self) -> None:
        should_be_affected = [
            'libintegration-tests' + DYLIB_SUFFIX,
        ]
        if self.cxx_tests_are_being_built():
            should_be_affected.append('linked_list-test')
        self.assert_all_affected_by(should_be_affected, 'tablet_server_main.cc')

        yb_master = self.get_yb_master_target()
        self.assert_all_unaffected_by([yb_master], 'tablet_server_main.cc')

    def test_call_home(self) -> None:
        yb_master = self.get_yb_master_target()
        yb_tserver = self.get_yb_tserver_target()
        self.assert_all_affected_by([yb_master], 'master_call_home.cc')
        self.assert_all_unaffected_by([yb_tserver], 'master_call_home.cc')
        self.assert_all_affected_by([yb_tserver], 'tserver_call_home.cc')
        if not self.is_lto():
            # This is only true in non-LTO mode. In LTO, yb-master and yb-tserver are the same
            # executable.
            self.assert_all_unaffected_by([yb_master], 'tserver_call_home.cc')

    def test_catalog_manager(self) -> None:
        yb_master = self.get_yb_master_target()
        yb_tserver = self.get_yb_tserver_target()
        self.assert_all_affected_by([yb_master], 'catalog_manager.cc')
        self.assert_all_unaffected_by([yb_tserver], 'catalog_manager.cc')

    def test_bulk_load_tool(self) -> None:
        exact_affected_set = [
            'yb-bulk_load',
            'yb-bulk_load.cc.o'
        ]
        if self.cxx_tests_are_being_built():
            exact_affected_set.append('yb-bulk_load-test')
        self.assert_exact_set_affected_by(exact_affected_set, 'yb-bulk_load.cc')

    def test_flex_bison(self) -> None:
        self.assert_all_affected_by([
                'scanner_lex.l.cc'
            ], 'scanner_lex.l')
        self.assert_all_affected_by([
                'parser_gram.y.cc'
            ], 'parser_gram.y')

    def test_proto_deps_validity(self) -> None:
        self.get_dep_graph().validate_proto_deps()


def run_self_test(dep_graph: DependencyGraph) -> None:
    logging.info("Running a self-test of the {} tool".format(os.path.basename(__file__)))
    DependencyGraphTest.set_dep_graph(dep_graph)
    suite = unittest.TestLoader().loadTestsFromTestCase(DependencyGraphTest)
    runner = unittest.TextTestRunner()
    result = runner.run(suite)
    if result.errors or result.failures:
        logging.info("Self-test of the dependency graph traversal tool failed!")
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description='A tool for working with the dependency graph')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable debug output')
    parser.add_argument('-r', '--rebuild-graph',
                        action='store_true',
                        help='Rebuild the dependecy graph and save it to a file')
    parser.add_argument('--node-type',
                        help='Node type to look for',
                        type=NodeType,
                        choices=list(NodeType),
                        default=NodeType.ANY)
    parser.add_argument('--file-regex',
                        help='Regular expression for file names to select as initial nodes for '
                             'querying the dependency graph.')
    parser.add_argument('--file-name-glob',
                        help='Like file-regex, but applies only to file name and uses the glob '
                             'syntax instead of regex.')
    parser.add_argument('--git-diff',
                        help='Figure out the list of files to use as starting points in the '
                             'dependency graph traversal by diffing the current state of the code '
                             'against this commit. This could also be anything that could be '
                             'passed to "git diff" as a single argument.')
    parser.add_argument('--git-commit',
                        help='Similar to --git-diff, but takes a git commit ref (e.g. sha1 or '
                             'branch) and uses the set of files from that commit.')
    parser.add_argument('--build-root',
                        required=True,
                        help='E.g. <some_root>/build/debug-clang14-dynamic-ninja')
    parser.add_argument('command',
                        type=Command,
                        choices=list(Command),
                        help='Command to perform')
    parser.add_argument('--output-test-config',
                        help='Output a "test configuration file", which is a JSON containing the '
                             'resulting list of C++ tests to run to this file, a flag indicating '
                             'wheter to run Java tests or not, etc.')
    parser.add_argument('--incomplete-build',
                        action='store_true',
                        help='Skip checking for file existence. Allows using the tool after '
                             'build artifacts have been deleted.')
    parser.add_argument('--build-args',
                        help='Extra arguments to pass to yb_build.sh. The build is invoked e.g. '
                             'if the compilation database file is missing.')
    parser.add_argument('--link-cmd-out-file',
                        help='For the %s command, write the linker arguments (one per line ) '
                             'to the given file.')
    parser.add_argument('--lto-output-suffix',
                        default="-lto",
                        help='The suffix to append to LTO-enabled binaries produced by '
                             'the %s command' % Command.LINK_WHOLE_PROGRAM.value)
    parser.add_argument('--lto-output-path',
                        help='Override the output file path for the LTO linking step.')
    parser.add_argument('--lto-type',
                        default='full',
                        choices=['full', 'thin'],
                        help='LTO type to use')
    parser.add_argument('--never-run-build',
                        action='store_true',
                        help='Never run yb_build.sh, even if the compilation database is missing.')
    parser.add_argument(
        '--run-linker',
        help='Whether to actually run the linker. Setting this to false might be useful when '
             'debugging, combined with --link-cmd-out-file.',
        type=arg_str_to_bool,
        default=True)
    parser.add_argument(
        '--symlink-as',
        help='Create a symlink with the given name pointing to the LTO output file, in the same '
             'directory as the output file. This option can be specified multiple times.',
        action='append')

    args = parser.parse_args()

    if args.file_regex and args.file_name_glob:
        raise RuntimeError('--file-regex and --file-name-glob are incompatible')

    cmd = args.command
    if (not args.file_regex and
            not args.file_name_glob and
            not args.rebuild_graph and
            not args.git_diff and
            not args.git_commit and
            cmd not in COMMANDS_NOT_NEEDING_TARGET_SET):
        raise RuntimeError(
                "Neither of --file-regex, --file-name-glob, --git-{diff,commit}, or "
                "--rebuild-graph are specified, and the command is not one of: " +
                ", ".join([cmd.value for cmd in COMMANDS_NOT_NEEDING_TARGET_SET]))

    log_level = logging.INFO
    logging.basicConfig(
        level=log_level,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")

    conf = DepGraphConf(
        verbose=args.verbose,
        build_root=args.build_root,
        incomplete_build=args.incomplete_build,
        file_regex=args.file_regex,
        file_name_glob=args.file_name_glob,
        build_args=args.build_args,
        never_run_build=args.never_run_build)
    if conf.file_regex and args.git_diff:
        raise RuntimeError(
                "--git-diff is incompatible with --file-{regex,name-glob}")

    if args.git_diff and args.git_commit:
        raise RuntimeError('--git-diff and --git-commit are incompatible')

    if args.git_commit:
        args.git_diff = "{}^..{}".format(args.git_commit, args.git_commit)

    graph_cache_path = os.path.join(args.build_root, 'dependency_graph.json')
    if args.rebuild_graph or not os.path.isfile(graph_cache_path):
        logging.info("Generating a dependency graph at '{}'".format(graph_cache_path))
        dep_graph_builder = DependencyGraphBuilder(conf)
        dep_graph = dep_graph_builder.build()
        dep_graph.save_as_json(graph_cache_path)
    else:
        start_time = datetime.now()
        with open(graph_cache_path) as graph_input_file:
            dep_graph = DependencyGraph(conf, json_data=json.load(graph_input_file))
        logging.info("Loaded dependency graph from '%s' in %.2f sec" %
                     (graph_cache_path, (datetime.now() - start_time).total_seconds()))
        dep_graph.validate_node_existence()

    # ---------------------------------------------------------------------------------------------
    # Commands that do not require an "initial set of targets"
    # ---------------------------------------------------------------------------------------------

    if cmd == Command.SELF_TEST:
        run_self_test(dep_graph)
        return
    if cmd == Command.DEBUG_DUMP:
        dep_graph.dump_debug_info()
        return

    # ---------------------------------------------------------------------------------------------
    # Figure out the initial set of targets based on a git commit, a regex, etc.
    # ---------------------------------------------------------------------------------------------

    updated_categories: Set[SourceFileCategory] = set()
    file_changes = []
    initial_nodes: Iterable[Node]
    if args.git_diff:
        old_working_dir = os.getcwd()
        with WorkDirContext(conf.yb_src_root):
            git_diff_output = subprocess.check_output(
                    ['git', 'diff', args.git_diff, '--name-only']).decode('utf-8')

            initial_nodes = set()
            file_paths = set()
            for file_path in git_diff_output.split("\n"):
                file_path = file_path.strip()
                if not file_path:
                    continue
                file_changes.append(file_path)
                # It is important that we invoke os.path.realpath with the current directory set to
                # the git repository root.
                file_path = os.path.realpath(file_path)
                file_paths.add(file_path)
                node = dep_graph.node_by_path.get(file_path)
                if node:
                    initial_nodes.add(node)

        if not initial_nodes:
            logging.warning("Did not find any graph nodes for this set of files: %s", file_paths)
            for basename in set([os.path.basename(file_path) for file_path in file_paths]):
                logging.warning("Nodes for basename '{}': {}".format(
                    basename, dep_graph.find_nodes_by_basename(basename)))

    elif conf.file_regex:
        logging.info("Using file name regex: {}".format(conf.file_regex))
        initial_nodes = dep_graph.find_nodes_by_regex(conf.file_regex)
        if not initial_nodes:
            logging.warning("Did not find any graph nodes for this pattern: %s", conf.file_regex)
        for node in initial_nodes:
            file_changes.append(node.path)
    else:
        raise RuntimeError("Could not figure out how to generate the initial set of files")

    file_changes = [
        (os.path.relpath(file_path, conf.yb_src_root) if os.path.isabs(file_path) else file_path)
        for file_path in file_changes
    ]

    if cmd == Command.LINK_WHOLE_PROGRAM:
        link_whole_program(
            dep_graph=dep_graph,
            initial_nodes=initial_nodes,
            link_cmd_out_file=args.link_cmd_out_file,
            run_linker=args.run_linker,
            lto_output_suffix=args.lto_output_suffix,
            lto_output_path=args.lto_output_path,
            lto_type=args.lto_type,
            symlink_as=args.symlink_as)
        return

    file_changes_by_category: Dict[SourceFileCategory, List[str]] = group_by(
        file_changes, get_file_category)

    # Same as file_changes_by_category, but with string values of categories instead of enum
    # elements.
    file_changes_by_category_str: Dict[str, List[str]] = {}
    for category, changes in file_changes_by_category.items():
        logging.info("File changes in category '%s':", category)
        for change in sorted(changes):
            logging.info("    %s", change)
        file_changes_by_category_str[category.value] = changes

    updated_categories = set(file_changes_by_category.keys())

    results: Set[Node] = set()
    if cmd == Command.AFFECTED:
        results = dep_graph.find_affected_nodes(set(initial_nodes), args.node_type)

    elif cmd == Command.DEPS:
        for node in initial_nodes:
            results.update(node.deps)
    elif cmd == Command.REVERSE_DEPS:
        for node in initial_nodes:
            results.update(node.reverse_deps)
    else:
        raise ValueError("Unimplemented command '{}'".format(cmd))

    if args.output_test_config:
        test_basename_list = sorted(
                [os.path.basename(node.path) for node in results
                 if node.node_type == NodeType.TEST])
        affected_basenames = set([os.path.basename(node.path) for node in results])

        # These are ALL tests, not just tests affected by the changes in question, used mostly
        # for logging.
        all_test_programs = [
            node for node in dep_graph.get_nodes() if node.node_type == NodeType.TEST]
        all_test_basenames = set([os.path.basename(node.path) for node in all_test_programs])

        # A very conservative way to decide whether to run all tests. If there are changes in any
        # categories (meaning the changeset is non-empty), and there are changes in categories other
        # than C++ / Java / files known not to affect unit tests, we force re-running all tests.
        unsafe_categories = updated_categories - CATEGORIES_NOT_CAUSING_RERUN_OF_ALL_TESTS
        user_said_all_tests = get_bool_env_var('YB_RUN_ALL_TESTS')

        test_filter_re = os.getenv('YB_TEST_EXECUTION_FILTER_RE')
        manual_test_filtering_with_regex = bool(test_filter_re)

        select_all_tests_for_now = (
            bool(unsafe_categories) or user_said_all_tests or manual_test_filtering_with_regex)

        user_said_all_cpp_tests = get_bool_env_var('YB_RUN_ALL_CPP_TESTS')
        user_said_all_java_tests = get_bool_env_var('YB_RUN_ALL_JAVA_TESTS')
        cpp_files_changed = SourceFileCategory.CPP in updated_categories
        java_files_changed = SourceFileCategory.JAVA in updated_categories
        yb_master_or_tserver_changed = bool(affected_basenames & set([
            'yb-master',
            'yb-tserver',
            'yb-master-dynamic',
            'yb-tserver-dynamic',
        ]))

        run_cpp_tests = select_all_tests_for_now or cpp_files_changed or user_said_all_cpp_tests

        run_java_tests = (
            select_all_tests_for_now or java_files_changed or yb_master_or_tserver_changed or
            user_said_all_java_tests)

        if select_all_tests_for_now:
            if user_said_all_tests:
                logging.info("User explicitly specified that all tests should be run")
            elif manual_test_filtering_with_regex:
                logging.info(
                    "YB_TEST_EXECUTION_FILTER_RE specified: %s, will filter tests at a later step",
                    test_filter_re)
            else:
                logging.info(
                    "All tests should be run based on file changes in these categories: {}".format(
                        ', '.join(sorted([category.value for category in unsafe_categories]))))
        else:
            if run_cpp_tests:
                if user_said_all_cpp_tests:
                    logging.info("User explicitly specified that all C++ tests should be run")
                else:
                    logging.info('Will run some C++ tests, some C++ files changed')
            if run_java_tests:
                if user_said_all_java_tests:
                    logging.info("User explicitly specified that all Java tests should be run")
                else:
                    logging.info('Will run all Java tests, ' +
                                 ' and '.join(
                                     (['some Java files changed'] if java_files_changed else []) +
                                     (['yb-{master,tserver} binaries changed']
                                      if yb_master_or_tserver_changed else [])))

        if run_cpp_tests and not test_basename_list and not select_all_tests_for_now:
            logging.info('There are no C++ test programs affected by the changes, '
                         'will skip running C++ tests.')
            run_cpp_tests = False

        test_conf = dict(
            run_cpp_tests=run_cpp_tests,
            run_java_tests=run_java_tests,
            file_changes_by_category=file_changes_by_category_str
        )
        if test_filter_re:
            test_conf.update(test_filter_re=test_filter_re)

        if not select_all_tests_for_now:
            # We only have this kind of fine-grained filtering for C++ test programs, and for Java
            # tests we either run all of them or none.
            test_conf['cpp_test_programs'] = test_basename_list
            if len(all_test_basenames) > 0:
                logging.info(
                    "{} C++ test programs should be run (out of {} possible, {}%)".format(
                        len(test_basename_list),
                        len(all_test_basenames),
                        "%.1f" % (100.0 * len(test_basename_list) / len(all_test_basenames))))
            if len(test_basename_list) != len(all_test_basenames):
                logging.info("The following C++ test programs will be run: {}".format(
                    ", ".join(sorted(test_basename_list))))

        with open(args.output_test_config, 'w') as output_file:
            output_file.write(json.dumps(test_conf, indent=2) + "\n")
        logging.info("Wrote a test configuration to {}".format(args.output_test_config))
    else:
        # For ad-hoc command-line use, mostly for testing and sanity-checking.
        for node in sorted(results, key=lambda node: [node.node_type.value, node.path]):
            print(node)
        logging.info("Found {} results".format(len(results)))


if __name__ == '__main__':
    main()
