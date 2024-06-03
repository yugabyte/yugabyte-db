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

import fnmatch
import logging
import os
import re
import json
import platform
import time
import random
import string

from enum import Enum
from typing import Any, Set, FrozenSet, List, Optional, Dict, Union, Iterable, FrozenSet

from yugabyte.common_util import (
    append_to_list_in_dict,
    assert_sets_equal,
    assert_set_contains_all,
    ensure_yb_src_root_from_build_root,
    get_build_type_from_build_root,
    get_home_dir,
    get_relative_path_or_none,
    group_by,
    is_ninja_build_root,
)

from yugabyte.cmake_cache import CMakeCache, load_cmake_cache


def make_extensions(exts_without_dot: List[str]) -> List[str]:
    """
    Make a list of extensions with leading dots from a list of extensions without leading dots.
    """
    for ext in exts_without_dot:
        assert not ext.startswith('.')

    return ['.' + ext for ext in exts_without_dot]


LIBRARY_FILE_EXTENSIONS_NO_DOT = ['so', 'dylib']
LIBRARY_FILE_EXTENSIONS = make_extensions(LIBRARY_FILE_EXTENSIONS_NO_DOT)

SOURCE_FILE_EXTENSIONS = make_extensions(
    ['c', 'cc', 'cpp', 'cxx', 'h', 'hpp', 'hxx', 'proto', 'l', 'y'])

LIBRARY_FILE_NAME_RE = re.compile(r'^lib(.*)[.](?:%s)$' % '|'.join(LIBRARY_FILE_EXTENSIONS_NO_DOT))
PROTO_LIBRARY_FILE_NAME_RE = re.compile(r'^lib(.*)_proto[.](?:%s)$' % '|'.join(
    LIBRARY_FILE_EXTENSIONS_NO_DOT))

TEST_FILE_SUFFIXES = ['_test', '-test', '_itest', '-itest']

EXECUTABLE_FILE_NAME_RE = re.compile(r'^[a-zA-Z0-9_.-]+$')

PROTO_GEN_OUTPUT_EXTENSIONS = ['pb', 'fwd', 'proxy', 'service', 'messages']
PROTO_OUTPUT_FILE_NAME_RE = re.compile(
    r'^([a-zA-Z_0-9-]+)[.](?:%s)[.](h|cc)$' % '|'.join(PROTO_GEN_OUTPUT_EXTENSIONS))

# Ignore some special-case CMake targets that do not have a one-to-one match with executables or
# libraries.
IGNORED_CMAKE_TARGETS = ['gen_version_info', 'latest_symlink', 'postgres']

DYLIB_SUFFIX = '.dylib' if platform.system() == 'Darwin' else '.so'


class NodeType(Enum):
    """
    We classify nodes in the dependency graph into multiple types.
    """

    # A special value used to match any node type.
    ANY = 'any'

    SOURCE = 'source'
    LIBRARY = 'library'
    TEST = 'test'
    OBJECT = 'object'
    EXECUTABLE = 'executable'
    OTHER = 'other'

    def __str__(self) -> str:
        return self.value

    def __lt__(self, other: Any) -> bool:
        assert isinstance(other, NodeType)
        return self.value < other.value

    def __eq__(self, other: Any) -> bool:
        assert isinstance(other, NodeType)
        return self.value == other.value

    def __hash__(self) -> int:
        return hash(self.value)


def is_object_file(path: str) -> bool:
    return path.endswith('.o')


def ends_with_any_of(path: str, exts: List[str]) -> bool:
    """
    Returns true if the given path ends with any of the given extensions.
    """
    for ext in exts:
        if path.endswith(ext):
            return True
    return False


def is_shared_library(path: str) -> bool:
    return (
        ends_with_any_of(path, LIBRARY_FILE_EXTENSIONS) and
        not os.path.basename(path) in LIBRARY_FILE_EXTENSIONS and
        not path.startswith('-'))


def get_node_type_by_path(path: str) -> NodeType:
    """
    >>> get_node_type_by_path('my_source_file.cc').value
    'source'
    >>> get_node_type_by_path('my_library.so').value
    'library'
    >>> get_node_type_by_path('/bin/bash').value
    'executable'
    >>> get_node_type_by_path('my_object_file.o').value
    'object'
    >>> get_node_type_by_path('tests-integration/some_file').value
    'test'
    >>> get_node_type_by_path('tests-integration/some_file.txt').value
    'other'
    >>> get_node_type_by_path('something/my-test').value
    'test'
    >>> get_node_type_by_path('something/my_test').value
    'test'
    >>> get_node_type_by_path('something/my-itest').value
    'test'
    >>> get_node_type_by_path('something/my_itest').value
    'test'
    >>> get_node_type_by_path('some-dir/some_file').value
    'other'
    """
    if ends_with_any_of(path, SOURCE_FILE_EXTENSIONS):
        return NodeType.SOURCE

    if (ends_with_any_of(path, LIBRARY_FILE_EXTENSIONS) or
            any([ext + '.' in path for ext in LIBRARY_FILE_EXTENSIONS])):
        return NodeType.LIBRARY

    if (ends_with_any_of(path, TEST_FILE_SUFFIXES) or
            (os.path.basename(os.path.dirname(path)).startswith('tests-') and
             '.' not in os.path.basename(path))):
        return NodeType.TEST

    if is_object_file(path):
        return NodeType.OBJECT

    if os.path.exists(path) and os.access(path, os.X_OK):
        # This will only work if the code has been fully built.
        return NodeType.EXECUTABLE

    return NodeType.OTHER


class DepGraphConf:
    verbose: bool
    build_root: str
    is_ninja: bool
    build_type: str
    yb_src_root: str
    src_dir_path: str
    rel_path_base_dirs: Set[str]
    incomplete_build: bool
    file_regex: Optional[str]
    never_run_build: bool

    # Extra arguments to pass to yb_build.sh.
    build_args: Optional[str]

    def __init__(
            self,
            verbose: bool,
            build_root: str,
            incomplete_build: bool,
            file_regex: Optional[str],
            file_name_glob: Optional[str],
            build_args: Optional[str],
            never_run_build: bool) -> None:
        self.verbose = verbose
        self.build_root = os.path.realpath(build_root)
        self.is_ninja = is_ninja_build_root(self.build_root)

        self.build_type = get_build_type_from_build_root(self.build_root)
        self.yb_src_root = ensure_yb_src_root_from_build_root(self.build_root)
        self.src_dir_path = os.path.join(self.yb_src_root, 'src')
        self.rel_path_base_dirs = {self.build_root, os.path.join(self.src_dir_path, 'yb')}
        self.incomplete_build = incomplete_build

        self.file_regex = None
        if file_regex:
            self.file_regex = file_regex
        elif file_name_glob:
            self.file_regex = fnmatch.translate('*/' + file_name_glob)

        if not os.path.isdir(self.src_dir_path):
            raise IOError(
                "Directory does not exist, or is not a directory: %s" % self.src_dir_path)
        self.build_args = build_args
        self.never_run_build = never_run_build


class Node:
    """
    A node in the dependency graph. Could be a source file, a header file, an object file, a
    dynamic library, or an executable.
    """

    path: str
    deps: Set['Node']
    reverse_deps: Set['Node']
    node_type: NodeType
    dep_graph: 'DependencyGraph'
    conf: 'DepGraphConf'
    source_str: Optional[str]
    is_proto_lib: bool
    link_cmd: Optional[List[str]]
    _cached_proto_lib_deps: Optional[List['Node']]
    _cached_containing_binaries: Optional[List['Node']]
    _cached_cmake_target: Optional[str]

    def __init__(self, path: str, dep_graph: 'DependencyGraph', source_str: Optional[str]) -> None:
        # Sometimes we might want to make yb-tserver and other binaries in the bin directory
        # symlinks to LTO-enabled binaries. That is why we don't want to use realpath here.
        path = os.path.abspath(path)
        self.path = path

        # Other nodes that this node depends on.
        self.deps = set()

        # Nodes that depend on this node.
        self.reverse_deps = set()

        self.node_type = get_node_type_by_path(path)
        self.dep_graph = dep_graph
        self.conf = dep_graph.conf
        self.source_str = source_str

        self.is_proto_lib = bool(
            self.node_type == NodeType.LIBRARY and
            PROTO_LIBRARY_FILE_NAME_RE.match(os.path.basename(self.path)))

        self._cached_proto_lib_deps = None
        self._cached_containing_binaries = None
        self._cached_cmake_target = None
        self._has_no_cmake_target = False

        self.link_cmd = None

    def add_dependency(self, dep: 'Node') -> None:
        assert self is not dep
        self.deps.add(dep)
        dep.reverse_deps.add(self)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, Node):
            return False

        return self.path == other.path

    def __hash__(self) -> int:
        return hash(self.path)

    def is_source(self) -> bool:
        return self.node_type == NodeType.SOURCE

    def validate_existence(self) -> None:
        if not os.path.exists(self.path) and not self.dep_graph.conf.incomplete_build:
            raise RuntimeError(
                    "Path does not exist: '{}'. This node was found in: {}. "
                    "The build might be incomplete or the dependency graph might be stale.".format(
                        self.path,
                        self.source_str))

    def get_pretty_path(self) -> str:
        for prefix, alias in [(self.conf.build_root, '$BUILD_ROOT'),
                              (self.conf.yb_src_root, '$YB_SRC_ROOT'),
                              (get_home_dir(), '~')]:
            if self.path.startswith(prefix + '/'):
                return alias + '/' + self.path[len(prefix) + 1:]

        return self.path

    def path_rel_to_build_root(self) -> Optional[str]:
        return get_relative_path_or_none(self.path, self.conf.build_root)

    def path_rel_to_src_root(self) -> Optional[str]:
        return get_relative_path_or_none(self.path, self.conf.yb_src_root)

    def __str__(self) -> str:
        return "Node(\"{}\", type={}, {} deps, {} rev deps)".format(
                self.get_pretty_path(), self.node_type, len(self.deps), len(self.reverse_deps))

    def __repr__(self) -> str:
        return self.__str__()

    def get_cmake_target(self) -> Optional[str]:
        """
        @return the CMake target based on the current node's path. E.g. this would be "master"
                for the "libmaster.so" library, "yb-master" for the "yb-master" executable.
        """
        if self._cached_cmake_target:
            return self._cached_cmake_target
        if self._has_no_cmake_target:
            return None

        if self.path.endswith('.proto'):
            path = self.path
            names = []
            while path != '/' and path != self.conf.yb_src_root:
                names.append(os.path.basename(path))
                path = os.path.dirname(path)

            # This needs to be consistent with the following CMake code snippet:
            #
            #   set(TGT_NAME "gen_${PROTO_REL_TO_YB_SRC_ROOT}")
            #   string(REPLACE "@" "_" TGT_NAME ${TGT_NAME})
            #   string(REPLACE "." "_" TGT_NAME ${TGT_NAME})
            #   string(REPLACE "-" "_" TGT_NAME ${TGT_NAME})
            #   string(REPLACE "/" "_" TGT_NAME ${TGT_NAME})
            #
            # (see FindProtobuf.cmake and FindYRPC.cmake).
            #
            # "/" cannot appear in the resulting string, so no need to replace it with "_".
            target = re.sub('[@.-]', '_', '_'.join(['gen'] + names[::-1]))

            if self.conf.verbose:
                logging.info("Associating protobuf file '{}' with CMake target '{}'".format(
                    self.path, target))
            self._cached_cmake_target = target
            return target

        basename = os.path.basename(self.path)
        m = LIBRARY_FILE_NAME_RE.match(basename)
        if m:
            target = m.group(1)
            self._cached_cmake_target = target
            return target
        m = EXECUTABLE_FILE_NAME_RE.match(basename)
        if m:
            self._cached_cmake_target = basename
            return basename
        self._has_no_cmake_target = True
        return None

    def get_containing_binaries(self) -> List['Node']:
        """
        Returns nodes (usually one node) corresponding to executables or dynamic libraries that the
        given object file is compiled into.
        """
        if self.path.endswith('.cc'):
            cc_o_rev_deps = [rev_dep for rev_dep in self.reverse_deps
                             if rev_dep.path.endswith('.cc.o')]
            if len(cc_o_rev_deps) != 1:
                raise RuntimeError(
                    "Could not identify exactly one object file reverse dependency of node "
                    "%s. All set of reverse dependencies: %s" % (self, self.reverse_deps))
            return cc_o_rev_deps[0].get_containing_binaries()

        if self.node_type != NodeType.OBJECT:
            raise ValueError(
                "get_containing_binaries can only be called on nodes of type 'object'. Node: " +
                str(self))

        if self._cached_containing_binaries:
            return self._cached_containing_binaries

        binaries = []
        for rev_dep in self.reverse_deps:
            if rev_dep.node_type in [NodeType.LIBRARY, NodeType.TEST, NodeType.EXECUTABLE]:
                binaries.append(rev_dep)
        if len(binaries) > 1:
            logging.warning(
                "Node %s is linked into multiple libraries: %s. Might be worth checking.",
                self, binaries)

        self._cached_containing_binaries = binaries
        return binaries

    def get_recursive_deps(
            self,
            skip_node_types: Union[Set[NodeType], FrozenSet[NodeType]] = frozenset()
            ) -> Set['Node']:
        """
        Returns a set of all dependencies that this node depends on.
        skip_node_types specifies the set of categories of nodes, except the initial node, to stop
        the recursive search at.
        """

        recursive_deps: Set[Node] = set()
        visited: Set[Node] = set()

        def walk(node: Node, is_initial: bool) -> None:
            if not is_initial:
                # Only skip the given subset of node types for nodes other than the initial node.
                if node.node_type in skip_node_types:
                    return
                # Also, never add the initial node to the result.
                recursive_deps.add(node)
            for dep in node.deps:
                if dep not in recursive_deps:
                    walk(dep, is_initial=False)

        walk(self, is_initial=True)
        return recursive_deps

    def get_proto_lib_deps(self) -> List['Node']:
        if self._cached_proto_lib_deps is None:
            self._cached_proto_lib_deps = [
                dep for dep in self.get_recursive_deps() if dep.is_proto_lib
            ]
        return self._cached_proto_lib_deps

    def get_containing_proto_lib(self) -> Optional['Node']:
        """
        For a .pb.cc file node, return the node corresponding to the containing protobuf library.
        """
        if not self.path.endswith('.pb.cc.o'):
            return None

        containing_binaries: List[Node] = self.get_containing_binaries()

        if len(containing_binaries) != 1:
            logging.info("Reverse deps:\n    %s" % ("\n    ".join(
                [str(dep) for dep in self.reverse_deps])))
            raise RuntimeError(
                "Invalid number of proto libs for %s. Expected 1 but found %d: %s" % (
                    self,
                    len(containing_binaries),
                    containing_binaries))
        return containing_binaries[0]

    def get_proto_gen_cmake_target(self) -> Optional[str]:
        """
        For .pb.{h,cc} nodes this will return a CMake target of the form
        gen_..., e.g. gen_src_yb_common_wire_protocol.
        """
        rel_path = self.path_rel_to_build_root()
        if not rel_path:
            return None

        basename = os.path.basename(rel_path)
        match = PROTO_OUTPUT_FILE_NAME_RE.match(basename)
        if not match:
            return None
        return '_'.join(
            ['gen'] +
            os.path.dirname(rel_path).split('/') +
            [match.group(1), 'proto']
        )

    def set_link_command(self, link_cmd: List[str]) -> None:
        assert self.link_cmd is None
        self.link_cmd = link_cmd

    def as_json(self) -> Dict[str, Any]:
        d: Dict[str, Any] = dict(
            path=self.path
        )
        if self.link_cmd:
            d['link_cmd'] = self.link_cmd
        return d

    def update_from_json(self, json_data: Dict[str, Any]) -> None:
        if 'link_cmd' in json_data:
            self.link_cmd = json_data['link_cmd']


class CMakeDepGraph:
    """
    A light-weight class representing the dependency graph of CMake targets imported from the
    yb_cmake_deps.txt file that we generate in our top-level CMakeLists.txt. This dependency graph
    does not have any nodes for source files and object files.
    """

    build_root: str
    cmake_targets: Set[str]
    cmake_deps_path: str
    cmake_deps: Dict[str, Set[str]]
    cmake_cache: CMakeCache

    def __init__(self, build_root: str) -> None:
        self.build_root = build_root
        self.cmake_deps_path = os.path.join(self.build_root, 'yb_cmake_deps.txt')
        self.cmake_cache = load_cmake_cache(self.build_root)
        self._load()

    def _load(self) -> None:
        logging.info("Loading dependencies between CMake targets from '{}'".format(
            self.cmake_deps_path))
        self.cmake_deps = {}
        self.cmake_targets = set()
        with open(self.cmake_deps_path) as cmake_deps_file:
            for line in cmake_deps_file:
                line = line.strip()
                if not line:
                    continue
                items = [item.strip() for item in line.split(':')]
                if len(items) != 2:
                    raise RuntimeError(
                            "Expected to find two items when splitting line on ':', found {}:\n{}",
                            len(items), line)
                lhs, rhs = items
                if lhs in IGNORED_CMAKE_TARGETS:
                    continue
                cmake_dep_set = self._get_cmake_dep_set_of(lhs)

                for cmake_dep in rhs.split(';'):
                    if cmake_dep in IGNORED_CMAKE_TARGETS:
                        continue
                    cmake_dep_set.add(cmake_dep)

        for cmake_target, cmake_target_deps in self.cmake_deps.items():
            if not cmake_target:
                raise ValueError("Empty CMake target found in {self.cmake_deps_path}")
            for cmake_target_dep in cmake_target_deps:
                if not cmake_target_dep:
                    raise ValueError(
                        f"Empty CMake target found as a dependency of {cmake_target} in "
                        f"{self.cmake_deps_path}")
            adding_targets = [cmake_target] + list(cmake_target_deps)
            self.cmake_targets.update(set(adding_targets))

        logging.info("Found {} CMake targets in '{}'".format(
            len(self.cmake_targets), self.cmake_deps_path))

    def _get_cmake_dep_set_of(self, target: str) -> Set[str]:
        """
        Get CMake dependencies of the given target. What is returned is a mutable set. Modifying
        this set modifies this CMake dependency graph.
        """
        deps = self.cmake_deps.get(target)
        if deps is None:
            deps = set()
            self.cmake_deps[target] = deps
            self.cmake_targets.add(target)
        return deps

    def add_dependency(self, from_target: str, to_target: str) -> None:
        if from_target in IGNORED_CMAKE_TARGETS or to_target in IGNORED_CMAKE_TARGETS:
            return
        self._get_cmake_dep_set_of(from_target).add(to_target)
        self.cmake_targets.add(to_target)

    def get_recursive_cmake_deps(self, cmake_target: str) -> Set[str]:
        result: Set[str] = set()
        visited: Set[str] = set()

        def walk(cur_target: str, add_this_target: bool = True) -> None:
            if cur_target in visited:
                return
            visited.add(cur_target)
            if add_this_target:
                result.add(cur_target)
            for dep in self.cmake_deps.get(cur_target, []):
                walk(dep)

        walk(cmake_target, add_this_target=False)
        return result


class DependencyGraph:

    conf: DepGraphConf
    node_by_path: Dict[str, Node]
    nodes_by_basename: Optional[Dict[str, Set[Node]]]
    cmake_dep_graph: Optional[CMakeDepGraph]

    canonicalization_cache: Dict[str, str] = {}

    cxx_tests_are_being_built_cached_value: Optional[bool]

    def __init__(self,
                 conf: DepGraphConf,
                 json_data: Optional[List[Dict[str, Any]]] = None) -> None:
        """
        @param json_data optional results of JSON parsing
        """
        self.conf = conf
        self.node_by_path = {}
        if json_data:
            self.init_from_json(json_data)
        self.nodes_by_basename = None
        self.cmake_dep_graph = None
        self.cxx_tests_are_being_built_cached_value = None

    def get_cmake_dep_graph(self) -> CMakeDepGraph:
        if self.cmake_dep_graph:
            return self.cmake_dep_graph

        cmake_dep_graph = CMakeDepGraph(self.conf.build_root)
        for node in self.get_nodes():
            proto_lib = node.get_containing_proto_lib()
            if proto_lib and self.conf.verbose:
                logging.info("node: %s, proto lib: %s", node, proto_lib)

        self.cmake_dep_graph = cmake_dep_graph
        return cmake_dep_graph

    def cxx_tests_are_being_built(self) -> bool:
        """
        Whether tests are being built. This is controlled by the --no-tests option to yb_build.sh.
        We are reading this information from CMakeCache.txt.
        """
        if self.cxx_tests_are_being_built_cached_value is None:
            self.cxx_tests_are_being_built_cached_value = \
                bool(self.get_cmake_dep_graph().cmake_cache.get_or_raise('YB_BUILD_TESTS'))
        return self.cxx_tests_are_being_built_cached_value

    def find_node(self,
                  path: str,
                  must_exist: bool = True,
                  source_str: Optional[str] = None) -> Node:
        assert source_str is None or not must_exist
        path = os.path.abspath(path)
        node = self.node_by_path.get(path)
        if node:
            return node
        if must_exist:
            raise RuntimeError(
                    ("Node not found by path: '{}' (expected to already have this node in our "
                     "graph, not adding).").format(path))
        node = Node(path, self, source_str)
        self.node_by_path[path] = node
        return node

    def find_or_create_node(self, path: str, source_str: Optional[str] = None) -> Node:
        """
        Finds a node with the given path or creates it if it does not exist.
        @param source_str a string description of how we came up with this node's path
        """

        canonical_path = self.canonicalization_cache.get(path)
        if not canonical_path:
            canonical_path = os.path.realpath(path)
            if canonical_path.startswith(self.conf.build_root + '/'):
                canonical_path = self.conf.build_root + '/' + \
                                 canonical_path[len(self.conf.build_root) + 1:]
            self.canonicalization_cache[path] = canonical_path

        return self.find_node(canonical_path, must_exist=False, source_str=source_str)

    def create_node_from_json(self, node_json_data: Dict[str, Any]) -> Node:
        node = self.find_or_create_node(node_json_data['path'])
        node.update_from_json(node_json_data)
        return node

    def init_from_json(self, json_nodes: List[Dict[str, Any]]) -> None:
        id_to_node = {}
        id_to_dep_ids = {}
        for node_json in json_nodes:
            node_id = node_json['id']
            id_to_node[node_id] = self.create_node_from_json(node_json)
            id_to_dep_ids[node_id] = node_json.get('deps') or []
        for node_id, dep_ids in id_to_dep_ids.items():
            node = id_to_node[node_id]
            for dep_id in dep_ids:
                node.add_dependency(id_to_node[dep_id])

    def find_nodes_by_regex(self, regex_str: str) -> List[Node]:
        filter_re = re.compile(regex_str)
        return [node for node in self.get_nodes() if filter_re.match(node.path)]

    def find_nodes_by_basename(self, basename: str) -> Optional[Set[Node]]:
        if not self.nodes_by_basename:
            # We are lazily initializing the basename -> node map, and any changes to the graph
            # after this point will not get reflected in it. This is OK as we're only using this
            # function after the graph has been built.
            self.nodes_by_basename = {
                k: set(v)
                for k, v in group_by(
                    self.get_nodes(),
                    lambda node: os.path.basename(node.path)).items()
            }
        assert self.nodes_by_basename is not None
        return self.nodes_by_basename.get(basename)

    def find_affected_nodes(
            self,
            start_nodes: Set[Node],
            requested_node_type: NodeType = NodeType.ANY) -> Set[Node]:
        if self.conf.verbose:
            logging.info("Starting with the following initial nodes:")
            for node in start_nodes:
                logging.info("    {}".format(node))

        results = set()
        visited = set()

        def dfs(node: Node) -> None:
            if ((requested_node_type == NodeType.ANY or
                 node.node_type == requested_node_type) and node not in start_nodes):
                results.add(node)
            if node in visited:
                return
            visited.add(node)
            for dep in node.reverse_deps:
                dfs(dep)

        for node in start_nodes:
            dfs(node)

        return results

    def affected_basenames_by_basename_for_test(
            self, basename: str, node_type: NodeType = NodeType.ANY) -> Set[str]:
        nodes_for_basename = self.find_nodes_by_basename(basename)
        if not nodes_for_basename:
            self.dump_debug_info()
            raise RuntimeError(
                    "No nodes found for file name '{}' (total number of nodes: {})".format(
                        basename, len(self.node_by_path)))
        return set([os.path.basename(node.path)
                    for node in self.find_affected_nodes(nodes_for_basename, node_type)])

    def save_as_json(self, output_path: str) -> None:
        """
        Converts the dependency graph into a JSON representation, where every node is given an id,
        so that dependencies are represented concisely.
        """
        output_path_tmp = ''.join([
            output_path,
            '.',
            str(int(time.time() * 1000000)),
            '.',
            ''.join([random.choice(string.ascii_letters) for _ in range(10)]),
            '.tmp'
        ])
        try:
            self._save_as_json_internal(output_path_tmp)
            os.rename(output_path_tmp, output_path)
        finally:
            if os.path.exists(output_path_tmp):
                os.remove(output_path_tmp)
        logging.info("Saved dependency graph to '{}'".format(output_path))

    def _save_as_json_internal(self, output_path: str) -> None:
        with open(output_path, 'w') as output_file:
            next_node_id = [1]  # Use a mutable object so we can modify it from closure.
            path_to_id: Dict[str, int] = {}
            output_file.write("[")

            def get_node_id(node: Node) -> int:
                node_id = path_to_id.get(node.path)
                if not node_id:
                    node_id = next_node_id[0]
                    path_to_id[node.path] = node_id
                    next_node_id[0] = node_id + 1
                return node_id

            is_first = True
            for node_path, node in self.node_by_path.items():
                node_json = node.as_json()
                dep_ids = [get_node_id(dep) for dep in node.deps]
                node_json.update(id=get_node_id(node))
                if dep_ids:
                    node_json.update(deps=dep_ids)
                if not is_first:
                    output_file.write(",\n")
                is_first = False
                output_file.write(json.dumps(node_json))
            output_file.write("\n]\n")

    def validate_node_existence(self) -> None:
        logging.info("Validating existence of build artifacts")
        for node in self.get_nodes():
            node.validate_existence()

    def get_nodes(self) -> Iterable[Node]:
        return self.node_by_path.values()

    def dump_debug_info(self) -> None:
        logging.info("Dumping all graph nodes for debugging ({} nodes):".format(
            len(self.node_by_path)))
        for node in sorted(self.get_nodes(), key=lambda node: str(node)):
            logging.info(node)

    def _add_proto_generation_deps(self) -> None:
        """
        Add dependencies of .pb.{h,cc} files on the corresponding .proto file. We do that by
        finding .proto and .pb.{h,cc} nodes in the graph independently and matching them
        based on their path relative to the source root.

        Additionally, we are inferring dependencies between binaries (protobuf libs or in some
        cases other libraries or even tests) that use a .pb.cc file on the CMake target that
        generates these files (e.g. gen_src_yb_rocksdb_db_version_edit_proto). We add these
        inferred dependencies to the separate CMake dependency graph.
        """
        proto_node_by_rel_path: Dict[str, Node] = {}
        pb_h_cc_nodes_by_rel_path: Dict[str, List[Node]] = {}

        cmake_dep_graph = self.get_cmake_dep_graph()
        assert cmake_dep_graph is not None

        for node in self.get_nodes():
            basename = os.path.basename(node.path)
            if node.path.endswith('.proto'):
                proto_rel_path = node.path_rel_to_src_root()
                if proto_rel_path:
                    proto_rel_path = proto_rel_path[:-6]
                    if proto_rel_path in proto_node_by_rel_path:
                        raise RuntimeError(
                            "Multiple .proto nodes found that share the same "
                            "relative path to source root: %s and %s" % (
                                proto_node_by_rel_path[proto_rel_path], node
                            ))

                    proto_node_by_rel_path[proto_rel_path] = node
            else:
                # Not a .proto file.
                match = PROTO_OUTPUT_FILE_NAME_RE.match(basename)
                if match:
                    proto_output_rel_path = node.path_rel_to_build_root()
                    if proto_output_rel_path:
                        append_to_list_in_dict(
                            pb_h_cc_nodes_by_rel_path,
                            os.path.join(
                                os.path.dirname(proto_output_rel_path),
                                match.group(1)
                            ),
                            node)
                        if node.path.endswith('.pb.cc'):
                            proto_gen_cmake_target = node.get_proto_gen_cmake_target()
                            for containing_binary in node.get_containing_binaries():
                                containing_cmake_target = containing_binary.get_cmake_target()
                                assert containing_cmake_target is not None
                                proto_gen_cmake_target = node.get_proto_gen_cmake_target()
                                assert proto_gen_cmake_target is not None
                                cmake_dep_graph.add_dependency(
                                    containing_cmake_target,
                                    proto_gen_cmake_target
                                )

        if not self.conf.incomplete_build:
            # Only for complete builds, we expect to find .pb.h and .pb.cc files for every .proto
            # file.
            for rel_path in proto_node_by_rel_path:
                if rel_path not in pb_h_cc_nodes_by_rel_path:
                    logging.error(
                        "For relative path %s, found a proto file (%s) but no .pb.{h,cc} files",
                        rel_path, proto_node_by_rel_path[rel_path])

        # Even for an incomplete build, we still expect to find a .proto file for each generated
        # .pb.h or .pb.cc file.
        for rel_path in pb_h_cc_nodes_by_rel_path:
            if rel_path not in proto_node_by_rel_path:
                logging.error(
                    "For relative path %s, found .pb.{h,cc} files (%s) but no .proto",
                    rel_path, pb_h_cc_nodes_by_rel_path[rel_path])

        proto_node_rel_path_set: Set[str] = set(proto_node_by_rel_path.keys())
        pb_h_cc_node_by_rel_path_set: Set[str] = set(pb_h_cc_nodes_by_rel_path.keys())
        if self.conf.incomplete_build:
            assert_set_contains_all(
                proto_node_rel_path_set,
                pb_h_cc_node_by_rel_path_set,
                message='This is a potentially incomplete build, which is OK. But we expect the '
                        'set of .proto file paths in the source directory to be a superset of the '
                        'set of .pb.h and .pb.cc files found in the build directory, ignoring '
                        'extensions.')
        else:
            assert_sets_equal(
                proto_node_rel_path_set,
                pb_h_cc_node_by_rel_path_set,
                message='This is a complete build. We expect to find .pb.{h,cc} files for every '
                        '.proto file and vice versa.')

        for rel_path in proto_node_by_rel_path.keys():
            proto_node = proto_node_by_rel_path[rel_path]
            # .pb.{h,cc} files need to depend on the .proto file they were generated from.
            for pb_h_cc_node in pb_h_cc_nodes_by_rel_path.get(rel_path, []):
                pb_h_cc_node.add_dependency(proto_node)

    def _check_for_circular_dependencies(self) -> None:
        logging.info("Checking for circular dependencies")
        visited = set()
        stack = []

        def walk(node: Node) -> None:
            if node in visited:
                return
            try:
                stack.append(node)
                visited.add(node)
                for dep in node.deps:
                    if dep in visited:
                        if dep in stack:
                            raise RuntimeError("Circular dependency loop found: %s", stack)
                        return
                    walk(dep)  # type: ignore
            finally:
                stack.pop()

        for node in self.get_nodes():
            walk(node)  # type: ignore

        logging.info("No circular dependencies found -- this is good (visited %d nodes)",
                     len(visited))

    def validate_proto_deps(self) -> None:
        """
        Make sure that code that depends on protobuf-generated files also depends on the
        corresponding protobuf library target.
        """

        logging.info("Validating dependencies on protobuf-generated headers")

        # TODO: only do this during graph generation.
        self._add_proto_generation_deps()
        self._check_for_circular_dependencies()

        # For any .pb.h file, we want to find all .cc.o files that depend on it, meaning that they
        # directly or indirectly include that .pb.h file.
        #
        # Then we want to look at the containing executable or library of the .cc.o file and make
        # sure it has a CMake dependency (direct or indirect) on the protobuf generation target of
        # the form gen_src_yb_common_redis_protocol_proto.
        #
        # However, we also need to get the CMake target name corresponding to the binary
        # containing the .cc.o file.

        proto_dep_errors = []

        PB_CC_SUFFIX = '.pb.cc'

        for node in self.get_nodes():
            if not node.path.endswith('.pb.cc.o'):
                continue
            source_deps = [dep for dep in node.deps if dep.path.endswith('.cc')]
            if len(source_deps) != 1:
                if not source_deps and self.conf.incomplete_build:
                    # This is a potentially incomplete build, and we might not have picked up the
                    # dependencies of certain .cc.o files on the corresponding .cc files.
                    # Omit this file from the analysis.
                    continue

                raise ValueError(
                    "Could not identify a single source dependency of node %s. Found: %s. " %
                    (node, source_deps))
            source_dep = source_deps[0]
            assert source_dep.path.endswith(PB_CC_SUFFIX)
            source_dep_path_prefix = source_dep.path[:-len(PB_CC_SUFFIX)]

            found_extensions_set: Set[str] = set()

            # The list of extensions of generated headers should match that in FindYRPC.cmake.
            for extension in PROTO_GEN_OUTPUT_EXTENSIONS:
                header_path = source_dep_path_prefix + '.' + extension + '.h'
                if header_path not in self.node_by_path:
                    if extension == 'pb':
                        raise ValueError(
                            'Graph node not found for protobuf-generated header: %s' % header_path)
                    # Other types of generated headers may or may not be present.
                    continue
                found_extensions_set.add(extension)
                header_node = self.node_by_path[header_path]
                proto_gen_target = header_node.get_proto_gen_cmake_target()
                if not proto_gen_target:
                    raise ValueError("proto_gen_target is None for node %s" % header_node)

                for rev_dep in header_node.reverse_deps:
                    if rev_dep.path.endswith('.cc.o'):
                        containing_binaries: Optional[List[Node]] = \
                            rev_dep.get_containing_binaries()
                        assert containing_binaries is not None
                        for binary in containing_binaries:
                            binary_cmake_target: Optional[str] = binary.get_cmake_target()
                            assert binary_cmake_target is not None

                            recursive_cmake_deps = \
                                self.get_cmake_dep_graph().get_recursive_cmake_deps(
                                    binary_cmake_target)
                            if proto_gen_target not in recursive_cmake_deps:
                                proto_dep_errors.append(
                                    f"CMake target {binary_cmake_target} does not depend directly "
                                    f"or indirectly on target {proto_gen_target} but uses the "
                                    f"header file {header_path}. Recursive cmake deps "
                                    f"of {binary_cmake_target}: {recursive_cmake_deps}"
                                )

        if proto_dep_errors:
            for error_msg in proto_dep_errors:
                logging.error("Protobuf dependency error: %s", error_msg)
            raise RuntimeError(
                "Found targets that use protobuf-generated header files but do not declare the "
                "dependency explicitly. See the log messages above.")
