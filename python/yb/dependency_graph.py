#!/usr/bin/env python

"""
Build a dependency graph of sources, object files, libraries, and binaries.  Compute the set of
tests that might be affected by changes to the given set of source files.
"""

from __future__ import print_function
from six import iteritems

import argparse
import fnmatch
import json
import logging
import os
import re
import subprocess
import sys
import unittest
from datetime import datetime

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from yb.common_util import group_by, make_set, get_build_type_from_build_root, \
                           convert_to_non_ninja_build_root, get_bool_env_var  # nopep8
from yb.command_util import mkdir_p  # nopep8


def make_extensions(exts_without_dot):
    return ['.' + ext for ext in exts_without_dot]


def ends_with_one_of(path, exts):
    for ext in exts:
        if path.endswith(ext):
            return True
    return False


SOURCE_FILE_EXTENSIONS = make_extensions(['c', 'cc', 'cpp', 'cxx', 'h', 'hpp', 'hxx', 'proto'])
LIBRARY_FILE_EXTENSIONS = make_extensions(['so', 'dylib'])
TEST_FILE_SUFFIXES = ['_test', '-test', '_itest', '-itest']
LIBRARY_FILE_NAME_RE = re.compile(r'^lib(.*)[.](?:so|dylib)$')
EXECUTABLE_FILE_NAME_RE = re.compile(r'^[a-zA-Z0-9_.-]+$')

# Ignore some special-case CMake targets that do not have a one-to-one match with executables or
# libraries.
IGNORED_CMAKE_TARGETS = ['gen_version_info', 'latest_symlink']

LIST_DEPS_CMD = 'deps'
LIST_REVERSE_DEPS_CMD = 'rev-deps'
LIST_AFFECTED_CMD = 'affected'
SELF_TEST_CMD = 'self-test'

COMMANDS = [LIST_DEPS_CMD,
            LIST_REVERSE_DEPS_CMD,
            LIST_AFFECTED_CMD,
            SELF_TEST_CMD]

HOME_DIR = os.path.realpath(os.path.expanduser('~'))

# This will match any node type (node types being sources/libraries/tests/etc.)
NODE_TYPE_ANY = 'any'

CATEGORY_DOES_NOT_AFFECT_TESTS = 'does_not_affect_tests'

# File changes in any category other than these will cause all tests to be re-run.  Even though
# changes to Python code affect the test framework, we can consider this Python code to be
# reasonably tested already, by running doctests, this script, the packaging script, etc.  We can
# remove "python" from this whitelist in the future.
CATEGORIES_NOT_CAUSING_RERUN_OF_ALL_TESTS = set(
        ['java', 'c++', 'python', CATEGORY_DOES_NOT_AFFECT_TESTS,
         # TODO: remove build_scripts from here (for testing only).
         'build_scripts'])


def is_object_file(path):
    return path.endswith('.o')


def get_node_type_by_path(path):
    """
    >>> get_node_type_by_path('my_source_file.cc')
    'source'
    >>> get_node_type_by_path('my_library.so')
    'library'
    >>> get_node_type_by_path('/bin/bash')
    'executable'
    >>> get_node_type_by_path('my_object_file.o')
    'object'
    >>> get_node_type_by_path('tests-integration/some_file')
    'test'
    >>> get_node_type_by_path('tests-integration/some_file.txt')
    'other'
    >>> get_node_type_by_path('something/my-test')
    'test'
    >>> get_node_type_by_path('something/my_test')
    'test'
    >>> get_node_type_by_path('something/my-itest')
    'test'
    >>> get_node_type_by_path('something/my_itest')
    'test'
    >>> get_node_type_by_path('some-dir/some_file')
    'other'
    """
    if ends_with_one_of(path, SOURCE_FILE_EXTENSIONS):
        return 'source'

    if ends_with_one_of(path, LIBRARY_FILE_EXTENSIONS):
        return 'library'

    if (ends_with_one_of(path, TEST_FILE_SUFFIXES) or
            (os.path.basename(os.path.dirname(path)).startswith('tests-') and
             '.' not in os.path.basename(path))):
        return 'test'

    if is_object_file(path):
        return 'object'

    if os.path.exists(path) and os.access(path, os.X_OK):
        # This will only work if the code has been fully built.
        return 'executable'

    return 'other'


class Node:
    """
    A node in the dependency graph. Could be a source file, a header file, an object file, a
    dynamic library, or an executable.
    """
    def __init__(self, path, dep_graph, source_str):
        path = os.path.realpath(path)
        self.path = path

        # Other nodes that this node depends on.
        self.deps = set()

        # Nodes that depend on this node.
        self.reverse_deps = set()

        self.node_type = get_node_type_by_path(path)
        self.dep_graph = dep_graph
        self.conf = dep_graph.conf
        self.source_str = source_str

    def add_dependency(self, dep):
        assert self is not dep
        self.deps.add(dep)
        dep.reverse_deps.add(self)

    def __eq__(self, other):
        if not isinstance(other, Node):
            return False

        return self.path == other.path

    def __hash__(self):
        return hash(self.path)

    def is_source(self):
        return self.node_type == 'source'

    def validate_existence(self):
        if not os.path.exists(self.path) and not self.dep_graph.conf.incomplete_build:
            raise RuntimeError(
                    "Path does not exist: '{}'. This node was found in: {}".format(
                        self.path, self.source_str))

    def get_pretty_path(self):
        for prefix, alias in [(self.conf.build_root, '$BUILD_ROOT'),
                              (self.conf.yb_src_root, '$YB_SRC_ROOT'),
                              (HOME_DIR, '~')]:
            if self.path.startswith(prefix + '/'):
                return alias + '/' + self.path[len(prefix) + 1:]

        return self.path

    def __str__(self):
        return "Node(\"{}\", type={}, {} deps, {} rev deps)".format(
                self.get_pretty_path(), self.node_type, len(self.deps), len(self.reverse_deps))

    def __repr__(self):
        return self.__str__()

    def get_cmake_target(self):
        """
        @return the CMake target based on the current node's path. E.g. this would be "master"
                for the "libmaster.so" library, "yb-master" for the "yb-master" executable.
        """
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
            return target

        basename = os.path.basename(self.path)
        m = LIBRARY_FILE_NAME_RE.match(basename)
        if m:
            return m.group(1)
        m = EXECUTABLE_FILE_NAME_RE.match(basename)
        if m:
            return basename
        return None


def set_to_str(items):
    return ",\n".join(sorted(items))


def is_abs_path(path):
    return path.startswith('/')


class Configuration:

    def __init__(self, args):
        self.args = args
        self.verbose = args.verbose
        self.build_root = os.path.realpath(args.build_root)

        # We can't use Ninja build files for dependency tracking. We will need to also create a
        # build directory with Make-compatible files. In practice, even this turns out not to be
        # sufficient, as we still need depend.make files that only get generated when we perform
        # the Make-based build, so in practice we just switch to using Make for Phabricator builds.
        # However, keeping this logic as we might find a way to use Ninja builds for dependency
        # tracking in the future.
        self.build_root_make = convert_to_non_ninja_build_root(self.build_root)
        self.build_type = get_build_type_from_build_root(self.build_root)
        self.yb_src_root = os.path.dirname(os.path.dirname(self.build_root))
        self.src_dir_path = os.path.join(self.yb_src_root, 'src')
        self.ent_src_dir_path = os.path.join(self.yb_src_root, 'ent', 'src')
        self.rel_path_base_dirs = set([self.build_root, os.path.join(self.src_dir_path, 'yb')])
        self.incomplete_build = args.incomplete_build

        self.file_regex = args.file_regex
        if not self.file_regex and args.file_name_glob:
            self.file_regex = fnmatch.translate('*/' + args.file_name_glob)

        assert os.path.exists(self.src_dir_path)
        assert os.path.exists(self.ent_src_dir_path)


class DependencyGraphBuilder:
    """
    Builds a dependency graph from the contents of the build directory. Each node of the graph is
    a file (an executable, a dynamic library, or a source file).
    """
    def __init__(self, conf):
        self.conf = conf
        self.compile_dirs = set()
        self.compile_commands = None
        self.useful_base_dirs = set()
        self.unresolvable_rel_paths = set()
        self.resolved_rel_paths = {}
        self.dep_graph = DependencyGraph(conf)
        self.cmake_deps = {}

    def load_cmake_deps(self):
        cmake_deps_path = os.path.join(self.conf.build_root, 'yb_cmake_deps.txt')
        logging.info("Loading dependencies between CMake targets from '{}'".format(
            cmake_deps_path))
        with open(cmake_deps_path) as cmake_deps_file:
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
                cmake_dep_set = self.cmake_deps.get(lhs)
                if not cmake_dep_set:
                    cmake_dep_set = set()
                    self.cmake_deps[lhs] = cmake_dep_set

                for cmake_dep in rhs.split(';'):
                    if cmake_dep in IGNORED_CMAKE_TARGETS:
                        continue
                    cmake_dep_set.add(cmake_dep)

        self.cmake_targets = set()
        for cmake_target, cmake_target_deps in iteritems(self.cmake_deps):
            self.cmake_targets.update(set([cmake_target] + list(cmake_target_deps)))
        logging.info("Found {} CMake targets in '{}'".format(
            len(self.cmake_targets), cmake_deps_path))

    def parse_link_and_depend_files(self):
        logging.info(
                "Parsing link.txt and depend.make files from the build tree at '{}'".format(
                    self.conf.build_root_make))
        start_time = datetime.now()

        num_parsed = 0
        for root, dirs, files in os.walk(self.conf.build_root_make):
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

    def find_proto_files(self):
        for src_subtree_root in [self.conf.src_dir_path, self.conf.ent_src_dir_path]:
            logging.info("Finding .proto files in the source tree at '{}'".format(src_subtree_root))
            source_str = 'proto files in {}'.format(src_subtree_root)
            for root, dirs, files in os.walk(src_subtree_root):
                for file_name in files:
                    if file_name.endswith('.proto'):
                        self.dep_graph.find_or_create_node(
                                os.path.join(root, file_name),
                                source_str=source_str)

    def match_cmake_targets_with_files(self):
        logging.info("Matching CMake targets with the files found")
        self.cmake_target_to_nodes = {}
        for node in self.dep_graph.get_nodes():
            node_cmake_target = node.get_cmake_target()
            if node_cmake_target:
                node_set = self.cmake_target_to_nodes.get(node_cmake_target)
                if not node_set:
                    node_set = set()
                    self.cmake_target_to_nodes[node_cmake_target] = node_set
                node_set.add(node)

        self.cmake_target_to_node = {}
        for cmake_target in self.cmake_targets:
            nodes = self.cmake_target_to_nodes.get(cmake_target)
            if not nodes:
                # List some potentially related nodes before we crash.
                for node in self.dep_graph.get_nodes():
                    if cmake_target in node.path:
                        logging.warning("Potentially matching node: {}".format(node))
                logging.warning("Total number of nodes: {}".format(len(self.dep_graph.get_nodes())))
                raise RuntimeError("Could not find file for CMake target '{}'".format(cmake_target))

            if len(nodes) > 1:
                raise RuntimeError("Ambiguous nodes found for CMake target '{}': {}".format(
                    cmake_target, nodes))
            self.cmake_target_to_node[cmake_target] = list(nodes)[0]

        # We're not adding nodes into our graph for CMake targets. Instead, we're finding files
        # that correspond to CMake targets, and add dependencies to those files.
        for cmake_target, cmake_target_deps in iteritems(self.cmake_deps):
            node = self.cmake_target_to_node[cmake_target]
            for cmake_target_dep in cmake_target_deps:
                node.add_dependency(self.cmake_target_to_node[cmake_target_dep])

    def resolve_rel_path(self, rel_path):
        if is_abs_path(rel_path):
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

    def resolve_dependent_rel_path(self, rel_path):
        if is_abs_path(rel_path):
            return rel_path
        if is_object_file(rel_path):
            return os.path.join(self.conf.build_root, rel_path)
        raise RuntimeError(
            "Don't know how to resolve relative path of a 'dependent': {}".format(
                rel_path))

    def parse_depend_file(self, depend_make_path):
        with open(depend_make_path) as depend_file:
            for line in depend_file:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                dependent, dependency = line.split(':')
                dependent = self.resolve_dependent_rel_path(dependent.strip())
                dependency = dependency.strip()
                dependency = self.resolve_rel_path(dependency)
                if dependency:
                    dependent_node = self.dep_graph.find_or_create_node(
                            dependent, source_str=depend_make_path)
                    dependency_node = self.dep_graph.find_or_create_node(
                            dependency, source_str=depend_make_path)
                    dependent_node.add_dependency(dependency_node)

    def find_node_by_rel_path(self, rel_path):
        if is_abs_path(rel_path):
            return self.find_node(rel_path, must_exist=True)
        candidates = []
        for path, node in iteritems(self.node_by_path):
            if path.endswith('/' + rel_path):
                candidates.append(node)
        if not candidates:
            raise RuntimeError("Could not find node by relative path '{}'".format(rel_path))
        if len(candidates) > 1:
            raise RuntimeError("Ambiguous nodes for relative path '{}'".format(rel_path))
        return candidates[0]

    def parse_link_txt_file(self, link_txt_path):
        assert link_txt_path.startswith(self.conf.build_root_make + '/')
        with open(link_txt_path) as link_txt_file:
            link_command = link_txt_file.read().strip()
        link_args = link_command.split()
        output_path = None
        inputs = []
        base_dir = os.path.join(os.path.dirname(link_txt_path), '..', '..')
        i = 0
        while i < len(link_args):
            arg = link_args[i]
            if arg == '-o':
                new_output_path = link_args[i + 1]
                if output_path and new_output_path and output_path != new_output_path:
                    raise RuntimeError(
                        "Multiple output paths for a link command ('{}' and '{}'): {}".format(
                            output_path, new_output_path, link_command))
                output_path = new_output_path
                i += 1
            else:
                if is_object_file(arg):
                    node = self.dep_graph.find_or_create_node(
                            os.path.abspath(os.path.join(base_dir, arg)))
                    inputs.append(node.path)

                if ends_with_one_of(arg, LIBRARY_FILE_EXTENSIONS) and not arg.startswith('-'):
                    node = self.dep_graph.find_or_create_node(
                            os.path.abspath(os.path.join(base_dir, arg)),
                            source_str=link_txt_path)
                    inputs.append(node.path)

            i += 1

        if not is_abs_path(output_path):
            output_path = os.path.abspath(os.path.join(base_dir, output_path))
        output_node = self.dep_graph.find_or_create_node(output_path, source_str=link_txt_path)
        output_node.validate_existence()
        for input_node in inputs:
            output_node.add_dependency(self.dep_graph.find_or_create_node(input_node))

    def build(self):
        compile_commands_path = os.path.join(self.conf.build_root_make, 'compile_commands.json')
        if not os.path.exists(compile_commands_path):

            # This is mostly useful during testing. We don't want to generate the list of compile
            # commands by default because it takes a while, so only generate it on demand.
            os.environ['CMAKE_EXPORT_COMPILE_COMMANDS'] = '1'
            mkdir_p(self.conf.build_root_make)

            os.environ['YB_USE_NINJA'] = '0'
            subprocess.check_call(
                    [os.path.join(self.conf.yb_src_root, 'yb_build.sh'),
                     self.conf.build_type,
                     '--cmake-only',
                     '--no-rebuild-thirdparty',
                     '--build-root', self.conf.build_root_make])

        logging.info("Loading compile commands from '{}'".format(compile_commands_path))
        with open(compile_commands_path) as commands_file:
            self.compile_commands = json.load(commands_file)

        for entry in self.compile_commands:
            self.compile_dirs.add(entry['directory'])

        self.parse_link_and_depend_files()
        self.find_proto_files()
        self.dep_graph.validate_node_existence()

        self.load_cmake_deps()
        self.match_cmake_targets_with_files()

        return self.dep_graph


class DependencyGraph:

    canonicalization_cache = {}

    def __init__(self, conf, json_data=None):
        """
        @param json_data optional results of JSON parsing
        """
        self.conf = conf
        self.node_by_path = {}
        if json_data:
            self.init_from_json(json_data)
        self.nodes_by_basename = None

    def find_node(self, path, must_exist=True, source_str=None):
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

    def find_or_create_node(self, path, source_str=None):
        """
        Finds a node with the given path or creates it if it does not exist.
        @param source_str a string description of how we came up with this node's path
        """

        canonical_path = self.canonicalization_cache.get(path)
        if not canonical_path:
            canonical_path = os.path.realpath(path)
            if canonical_path.startswith(self.conf.build_root_make + '/'):
                canonical_path = self.conf.build_root + '/' + \
                                 canonical_path[len(self.conf.build_root_make) + 1:]
            self.canonicalization_cache[path] = canonical_path

        return self.find_node(canonical_path, must_exist=False, source_str=source_str)

    def init_from_json(self, json_nodes):
        id_to_node = {}
        id_to_dep_ids = {}
        for node_json in json_nodes:
            node_id = node_json['id']
            id_to_node[node_id] = self.find_or_create_node(node_json['path'])
            id_to_dep_ids[node_id] = node_json['deps']
        for node_id, dep_ids in iteritems(id_to_dep_ids):
            node = id_to_node[node_id]
            for dep_id in dep_ids:
                node.add_dependency(id_to_node[dep_id])

    def find_nodes_by_regex(self, regex_str):
        filter_re = re.compile(regex_str)
        return [node for node in self.get_nodes() if filter_re.match(node.path)]

    def find_nodes_by_basename(self, basename):
        if not self.nodes_by_basename:
            # We are lazily initializing the basename -> node map, and any changes to the graph
            # after this point will not get reflected in it. This is OK as we're only using this
            # function after the graph has been built.
            self.nodes_by_basename = group_by(
                    self.get_nodes(),
                    lambda node: os.path.basename(node.path))
        return self.nodes_by_basename.get(basename)

    def find_affected_nodes(self, start_nodes, requested_node_type=NODE_TYPE_ANY):
        if self.conf.verbose:
            logging.info("Starting with the following initial nodes:")
            for node in start_nodes:
                logging.info("    {}".format(node))

        results = set()
        visited = set()

        def dfs(node):
            if ((requested_node_type == NODE_TYPE_ANY or node.node_type == requested_node_type) and
                    node not in start_nodes):
                results.add(node)
            if node in visited:
                return
            visited.add(node)
            for dep in node.reverse_deps:
                dfs(dep)

        for node in start_nodes:
            dfs(node)

        return results

    def affected_basenames_by_basename_for_test(self, basename, node_type=NODE_TYPE_ANY):
        nodes_for_basename = self.find_nodes_by_basename(basename)
        if not nodes_for_basename:
            self.dump_debug_info()
            raise RuntimeError(
                    "No nodes found for file name '{}' (total number of nodes: {})".format(
                        basename, len(self.node_by_path)))
        return set([os.path.basename(node.path)
                    for node in self.find_affected_nodes(nodes_for_basename, node_type)])

    def save_as_json(self, output_path):
        """
        Converts the dependency graph into a JSON representation, where every node is given an id,
        so that dependencies are represented concisely.
        """
        with open(output_path, 'w') as output_file:
            next_node_id = [1]  # Use a mutable object so we can modify it from closure.
            path_to_id = {}
            output_file.write("[")

            def get_node_id(node):
                node_id = path_to_id.get(node.path)
                if not node_id:
                    node_id = next_node_id[0]
                    path_to_id[node.path] = node_id
                    next_node_id[0] = node_id + 1
                return node_id

            is_first = True
            for node_path, node in iteritems(self.node_by_path):
                node_json = dict(
                    id=get_node_id(node),
                    path=node_path,
                    deps=[get_node_id(dep) for dep in node.deps]
                    )
                if not is_first:
                    output_file.write(",\n")
                is_first = False
                output_file.write(json.dumps(node_json))
            output_file.write("\n]\n")

        logging.info("Saved dependency graph to '{}'".format(output_path))

    def validate_node_existence(self):
        logging.info("Validating existence of build artifacts")
        for node in self.get_nodes():
            node.validate_existence()

    def get_nodes(self):
        return self.node_by_path.values()

    def dump_debug_info(self):
        logging.info("Dumping all graph nodes for debugging ({} nodes):".format(
            len(self.node_by_path)))
        for node in sorted(self.get_nodes(), key=lambda node: str(node)):
            logging.info(node)


class DependencyGraphTest(unittest.TestCase):
    dep_graph = None

    # Basename -> basenames affected by it.
    affected_basenames_cache = {}

    def get_affected_basenames(self, initial_basename):
        affected_basenames = self.affected_basenames_cache.get(initial_basename)
        if not affected_basenames:
            affected_basenames = self.dep_graph.affected_basenames_by_basename_for_test(
                    initial_basename)
            self.affected_basenames_cache[initial_basename] = affected_basenames
            if self.dep_graph.conf.verbose:
                # This is useful to get inspiration for new tests.
                logging.info("Files affected by {}:\n    {}".format(
                    initial_basename, "\n    ".join(sorted(affected_basenames))))
        return affected_basenames

    def assert_affected_by(self, expected_affected_basenames, initial_basename):
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

    def assert_unaffected_by(self, unaffected_basenames, initial_basename):
        """
        Asserts that the given files are unaffected by the given file.
        """
        affected_basenames = self.get_affected_basenames(initial_basename)
        incorrectly_affected = make_set(unaffected_basenames) & affected_basenames
        if incorrectly_affected:
            self.assertFalse(
                    ("Expected files {} to be unaffected by {}, but they are. Other affected "
                     "files: {}").format(
                         sorted(incorrectly_affected),
                         initial_basename,
                         sorted(affected_basenames - incorrectly_affected)))

    def assert_affected_exactly_by(self, expected_affected_basenames, initial_basename):
        """
        Checks the exact set of files affected by the given file.
        """
        affected_basenames = self.get_affected_basenames(initial_basename)
        self.assertEquals(make_set(expected_affected_basenames), affected_basenames)

    def test_master_main(self):
        self.assert_affected_by([
                'libintegration-tests.so',
                'yb-master'
            ], 'master_main.cc')

    def test_tablet_server_main(self):
        self.assert_affected_by([
                'libintegration-tests.so',
                'linked_list-test'
            ], 'tablet_server_main.cc')

        self.assert_unaffected_by(['yb-master'], 'tablet_server_main.cc')

    def test_bulk_load_tool(self):
        self.assert_affected_exactly_by([
                'yb-bulk_load',
                'yb-bulk_load-test',
                'yb-bulk_load.cc.o'
            ], 'yb-bulk_load.cc')


def run_self_test(dep_graph):
    logging.info("Running a self-test of the {} tool".format(os.path.basename(__file__)))
    DependencyGraphTest.dep_graph = dep_graph
    suite = unittest.TestLoader().loadTestsFromTestCase(DependencyGraphTest)
    runner = unittest.TextTestRunner()
    result = runner.run(suite)
    if result.errors or result.failures:
        logging.info("Self-test of the dependency graph traversal tool failed!")
        sys.exit(1)


def get_file_category(rel_path):
    """
    Categorize file changes so that we can decide what tests to run.

    @param rel_path: path relative to the source root (not to the build root)
    """
    basename = os.path.basename(rel_path)

    if rel_path.startswith('bin/'):
        # As of December 2017, there is nothing in the "bin" directory that is being used by tests.
        # If that changes, this needs to be updated. Note that the "bin" directory here is the
        # yugabyte/bin directory in the source tree, not the "bin" directory under the build
        # directory, so it only has scripts and not yb-master / yb-tserver binaries.
        return CATEGORY_DOES_NOT_AFFECT_TESTS

    if rel_path == 'yb_build.sh':
        # The main build script is being run anyway.
        return CATEGORY_DOES_NOT_AFFECT_TESTS

    if basename == 'CMakeLists.txt' or basename.endswith('.cmake'):
        return 'cmake'

    if rel_path.startswith('src/') or rel_path.startswith('ent/src/'):
        return 'c++'

    if rel_path.startswith('python/'):
        return 'python'

    # Top-level subdirectories that map one-to-one to categories.
    for category_dir in ['java', 'thirdparty']:
        if rel_path.startswith(category_dir + '/'):
            return category_dir

    if rel_path.startswith('build-support/'):
        return 'build_scripts'
    return 'other'


def main():
    parser = argparse.ArgumentParser(
        description='A tool for working with the dependency graph')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable debug output')
    parser.add_argument('-r', '--rebuild-graph',
                        action='store_true',
                        help='Rebuild the dependecy graph and save it to a file')
    parser.add_argument('--node-type',
                        help='Node type to look for',
                        default='any',
                        choices=['test', 'object', 'library', 'source', 'any'])
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
                        help='E.g. <some_root>/build/debug-gcc-dynamic-community')
    parser.add_argument('command',
                        choices=COMMANDS,
                        help='Command to perform')
    parser.add_argument('--output-test-config',
                        help='Output a "test configuration file", which is a JSON containing the '
                             'resulting list of C++ tests to run to this file, a flag indicating '
                             'wheter to run Java tests or not, etc.')
    parser.add_argument('--incomplete-build',
                        action='store_true',
                        help='Skip checking for file existence. Allows using the tool after '
                             'build artifacts have been deleted.')
    args = parser.parse_args()

    if args.file_regex and args.file_name_glob:
        raise RuntimeError('--file-regex and --file-name-glob are incompatible')

    cmd = args.command
    if (not args.file_regex and
            not args.file_name_glob and
            not args.rebuild_graph and
            not args.git_diff and
            not args.git_commit and
            cmd != SELF_TEST_CMD):
        raise RuntimeError(
                "Neither of --file-regex, --file-name-glob, --git-{diff,commit}, or "
                "--rebuild-graph are specified, and the command is not " + SELF_TEST_CMD)

    log_level = logging.INFO
    logging.basicConfig(
        level=log_level,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")

    conf = Configuration(args)
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

    if cmd == SELF_TEST_CMD:
        run_self_test(dep_graph)
        return

    updated_categories = None
    file_changes = []
    if args.git_diff:
        old_working_dir = os.getcwd()
        os.chdir(conf.yb_src_root)
        git_diff_output = subprocess.check_output(
                ['git', 'diff', args.git_diff, '--name-only'])

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

        os.chdir(old_working_dir)

        if not initial_nodes:
            logging.warning("Did not find any graph nodes for this set of files: {}".format(
                file_paths))
            for basename in set([os.path.basename(file_path) for file_path in file_paths]):
                logging.warning("Nodes for basename '{}': {}".format(
                    basename, dep_graph.find_nodes_by_basename(basename)))

        file_changes_by_category = group_by(file_changes, get_file_category)
        for category, changes in file_changes_by_category.items():
            logging.info("File changes in category '{}':".format(category))
            for change in sorted(changes):
                logging.info("    {}".format(change))
        updated_categories = set(file_changes_by_category.keys())

    elif conf.file_regex:
        logging.info("Using file name regex: {}".format(conf.file_regex))
        initial_nodes = dep_graph.find_nodes_by_regex(conf.file_regex)
    else:
        raise RuntimeError("Could not figure out how to generate the initial set of files")

    results = set()
    if cmd == LIST_AFFECTED_CMD:
        results = dep_graph.find_affected_nodes(initial_nodes, args.node_type)
    elif cmd == LIST_DEPS_CMD:
        for node in initial_nodes:
            results.update(node.deps)
    elif cmd == LIST_REVERSE_DEPS_CMD:
        for node in initial_nodes:
            results.update(node.reverse_deps)
    else:
        raise RuntimeError("Unimplemented command '{}'".format(command))

    if args.output_test_config:
        test_basename_list = sorted(
                [os.path.basename(node.path) for node in results if node.node_type == 'test'])
        affected_basenames = set([os.path.basename(node.path) for node in results])

        # These are ALL tests, not just tests affected by the changes in question, used mostly
        # for logging.
        all_test_programs = [node for node in dep_graph.get_nodes() if node.node_type == 'test']
        all_test_basenames = set([os.path.basename(node.path) for node in all_test_programs])

        # A very conservative way to decide whether to run all tests. If there are changes in any
        # categories (meaning the changeset is non-empty), and there are changes in categories other
        # than C++ / Java / files known not to affect unit tests, we force re-running all tests.
        unsafe_categories = updated_categories - CATEGORIES_NOT_CAUSING_RERUN_OF_ALL_TESTS
        user_said_all_tests = get_bool_env_var('YB_RUN_ALL_TESTS')
        run_all_tests = bool(unsafe_categories) or user_said_all_tests

        user_said_all_cpp_tests = get_bool_env_var('YB_RUN_ALL_CPP_TESTS')
        user_said_all_java_tests = get_bool_env_var('YB_RUN_ALL_JAVA_TESTS')
        cpp_files_changed = 'c++' in updated_categories
        java_files_changed = 'java' in updated_categories
        yb_master_or_tserver_changed = bool(affected_basenames & set(['yb-master', 'yb-tserver']))

        run_cpp_tests = run_all_tests or cpp_files_changed or user_said_all_cpp_tests
        run_java_tests = (
                run_all_tests or java_files_changed or yb_master_or_tserver_changed or
                user_said_all_java_tests
            )

        if run_all_tests:
            if user_said_all_tests:
                logging.info("User explicitly specified that all tests should be run")
            else:
                logging.info(
                    "All tests should be run based on file changes in these categories: {}".format(
                        ', '.join(sorted(unsafe_categories))))
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

        test_conf = dict(
            run_cpp_tests=run_cpp_tests,
            run_java_tests=run_java_tests,
            file_changes_by_category=file_changes_by_category
        )
        if not run_all_tests:
            test_conf['cpp_test_programs'] = test_basename_list
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
        for node in sorted(results, key=lambda node: [node.node_type, node.path]):
            print(node)
        logging.info("Found {} results".format(len(results)))


if __name__ == '__main__':
    main()
