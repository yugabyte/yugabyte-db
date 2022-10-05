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

import collections
import copy
import logging
import os
import re
import time
import glob
import subprocess
import random
import sys
import tempfile
import atexit
import glob
import argparse

from functools import total_ordering

from yb import command_util
from yb.common_util import get_build_type_from_build_root, \
                           get_compiler_type_from_build_root, \
                           is_macos  # nopep8
from typing import Optional, List, Set, Dict, cast

# This is used to separate relative binary path from gtest_filter for C++ tests in what we call
# a "test descriptor" (a string that identifies a particular test).
#
# This must match the constant with the same name in common-test-env.sh.
TEST_DESCRIPTOR_SEPARATOR = ":::"
BINARY_AND_TEST_NAME_SEPARATOR = "__"

JAVA_TEST_DESCRIPTOR_RE = re.compile(r'^([a-z0-9-]+)/src/test/(?:java|scala)/(.*)$')

TEST_DESCRIPTOR_ATTEMPT_PREFIX = TEST_DESCRIPTOR_SEPARATOR + 'attempt_'
TEST_DESCRIPTOR_ATTEMPT_INDEX_RE = re.compile(
    r'^(.*)' + TEST_DESCRIPTOR_ATTEMPT_PREFIX + r'(\d+)$')

global_conf: Optional['GlobalTestConfig'] = None

CLOCK_SYNC_WAIT_LOGGING_INTERVAL_SEC = 10

MAX_TIME_TO_WAIT_FOR_CLOCK_SYNC_SEC = 60


@total_ordering
class TestDescriptor:
    """
    A "test descriptor" identifies a particular test we could run on a distributed test worker.
    A string representation of a "test descriptor" is an optional "attempt_<index>:::" followed by
    one of the options below:
    - A C++ test program name relative to the build root. This implies running all tests within
      the test program. This has the disadvantage that a failure of one of those tests will cause
      the rest of tests not to be run.
    - A C++ test program name relative to the build root followed by the ':::' separator and the
      gtest filter identifying a test within that test program,
    - A string like 'com.yugabyte.jedis.TestYBJedis#testPool[1]' describing a Java test. This is
      something that could be passed directly to the -Dtest=... Maven option.
    - A Java test class source path (including .java/.scala extension) relative to the "java"
      directory in the YugabyteDB source tree.
    """
    descriptor_str_without_attempt_index: str
    attempt_index: int
    is_jvm_based: bool
    language: str
    args_for_run_test: str
    error_output_path: str

    def __init__(self, descriptor_str: str) -> None:
        assert global_conf is not None
        self.descriptor_str = descriptor_str

        attempt_index_match = TEST_DESCRIPTOR_ATTEMPT_INDEX_RE.match(descriptor_str)
        if attempt_index_match:
            self.attempt_index = int(attempt_index_match.group(2))
            self.descriptor_str_without_attempt_index = attempt_index_match.group(1)
        else:
            self.attempt_index = 1
            self.descriptor_str_without_attempt_index = descriptor_str

        self.is_jvm_based = False
        is_mvn_compatible_descriptor = False

        if len(self.descriptor_str.split('#')) == 2:
            self.is_jvm_based = True
            # Could be Scala, but as of 08/2018 we only have Java tests in the repository.
            self.language = 'Java'
            is_mvn_compatible_descriptor = True
        elif self.descriptor_str.endswith('.java'):
            self.is_jvm_based = True
            self.language = 'Java'
        elif self.descriptor_str.endswith('.scala'):
            self.is_jvm_based = True
            self.language = 'Scala'

        if self.is_jvm_based:
            # This is a Java/Scala test.
            if is_mvn_compatible_descriptor:
                # This is a string of the form "com.yugabyte.jedis.TestYBJedis#testPool[1]".
                self.args_for_run_test = self.descriptor_str
                output_file_name = self.descriptor_str
            else:
                # The "test descriptors string " is the Java source file path relative to the "java"
                # directory.
                java_descriptor_match = JAVA_TEST_DESCRIPTOR_RE.match(
                    self.descriptor_str_without_attempt_index)
                if java_descriptor_match is None:
                    raise ValueError(
                        f"Java/Scala test descriptor {self.descriptor_str_without_attempt_index} "
                        f"could not be parsed using the regular expression "
                        f"{JAVA_TEST_DESCRIPTOR_RE}")

                mvn_module, package_and_class_with_slashes = java_descriptor_match.groups()

                package_and_class = package_and_class_with_slashes.replace('/', '.')
                self.args_for_run_test = "{} {}".format(mvn_module, package_and_class)
                output_file_name = package_and_class
        else:
            self.language = 'C++'
            test_name: Optional[str]

            # This is a C++ test.
            if TEST_DESCRIPTOR_SEPARATOR in self.descriptor_str_without_attempt_index:
                rel_test_binary, test_name = self.descriptor_str_without_attempt_index.split(
                    TEST_DESCRIPTOR_SEPARATOR)
            else:
                rel_test_binary = self.descriptor_str_without_attempt_index
                test_name = None

            # Arguments for run-test.sh.
            # - The absolute path to the test binary (the test descriptor only contains the relative
            #   path).
            # - Optionally, the gtest filter within the test program.
            self.args_for_run_test = os.path.join(global_conf.build_root, rel_test_binary)
            if test_name:
                self.args_for_run_test += " " + test_name
            output_file_name = rel_test_binary
            if test_name:
                output_file_name += BINARY_AND_TEST_NAME_SEPARATOR + test_name

        output_file_name = re.sub(r'[\[\]/#]', '_', output_file_name)
        self.error_output_path = os.path.join(
                global_conf.build_root, 'yb-test-logs', output_file_name + '__error.log')

    def __str__(self) -> str:
        if self.attempt_index == 1:
            return self.descriptor_str_without_attempt_index
        return ''.join([
            self.descriptor_str_without_attempt_index,
            TEST_DESCRIPTOR_ATTEMPT_PREFIX,
            str(self.attempt_index)
        ])

    def str_for_file_name(self) -> str:
        return str(self).replace('/', '__').replace(':', '_')

    def __eq__(self, other: object) -> bool:
        other_descriptor = cast(TestDescriptor, other)
        return self.descriptor_str == other_descriptor.descriptor_str

    def __ne__(self, other: object) -> bool:
        return not (self == other)

    def __lt__(self, other: object) -> bool:
        other_descriptor = cast(TestDescriptor, other)
        return self.descriptor_str < other_descriptor.descriptor_str

    def with_attempt_index(self, attempt_index: int) -> 'TestDescriptor':
        assert attempt_index >= 1
        copied = copy.copy(self)
        copied.attempt_index = attempt_index
        # descriptor_str is just the cached version of the string representation, with the
        # attempt_index included (if it is greater than 1).
        copied.descriptor_str = str(copied)
        return copied


class GlobalTestConfig:
    build_root: str
    build_type: str
    yb_src_root: str
    archive_for_workers: Optional[str]
    rel_build_root: str
    archive_sha256sum: Optional[str]
    compiler_type: str

    def __init__(
            self,
            build_root: str,
            build_type: str,
            yb_src_root: str,
            archive_for_workers: Optional[str],
            rel_build_root: str,
            archive_sha256sum: Optional[str],
            compiler_type: str) -> None:
        self.build_root = os.path.abspath(build_root)
        self.build_type = build_type
        self.yb_src_root = yb_src_root
        self.archive_for_workers = archive_for_workers
        self.rel_build_root = rel_build_root
        self.archive_sha256sum = archive_sha256sum
        self.compiler_type = compiler_type

    def get_run_test_script_path(self) -> str:
        return os.path.join(self.yb_src_root, 'build-support', 'run-test.sh')

    def set_env_on_spark_worker(
            self, propagated_env_vars: Dict[str, str] = {}) -> None:
        """
        Used on the distributed worker side (inside functions that run on Spark) to configure the
        necessary environment.
        """
        os.environ['BUILD_ROOT'] = os.path.abspath(self.build_root)
        os.environ['YB_COMPILER_TYPE'] = self.compiler_type
        # This is how we tell run-test.sh what set of C++ binaries to use for mini-clusters in Java
        # tests.
        for env_var_name, env_var_value in propagated_env_vars.items():
            os.environ[env_var_name] = env_var_value


TestResult = collections.namedtuple(
        'TestResult',
        ['test_descriptor',
         'exit_code',
         'elapsed_time_sec',
         'failed_without_output',
         'artifact_paths',
         'num_errors_copying_artifacts'])


def set_global_conf_from_args(args: argparse.Namespace) -> GlobalTestConfig:
    build_root = os.path.realpath(args.build_root)

    # This module is expected to be under python/yb.
    yb_src_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

    # Ensure that build_root is consistent with yb_src_root above.
    yb_src_root_from_build_root = os.path.dirname(os.path.dirname(build_root))

    build_type = get_build_type_from_build_root(build_root)

    archive_for_workers = None
    if args.send_archive_to_workers:
        archive_for_workers = os.path.abspath(os.path.join(
            build_root, 'archive_for_tests_on_spark.tar.gz.spark-no-extract'))

    assert yb_src_root == yb_src_root_from_build_root, \
        ("An inconstency between YB_SRC_ROOT derived from module location ({}) vs. the one derived "
         "from BUILD_ROOT ({})").format(yb_src_root, yb_src_root_from_build_root)

    rel_build_root = os.path.relpath(
            os.path.abspath(build_root),
            os.path.abspath(yb_src_root))
    if len(rel_build_root.split('/')) != 2:
        raise ValueError(
                "Unexpected number of components in the relative path of build root to "
                "source root: %s. build_root=%s, yb_src_root=%s" % (
                    rel_build_root, build_root, yb_src_root))

    compiler_type = get_compiler_type_from_build_root(build_root)
    compiler_type_from_env = os.environ.get('YB_COMPILER_TYPE')
    if compiler_type_from_env is not None and compiler_type_from_env != compiler_type:
        raise ValueError(
                "Build root '%s' implies compiler type '%s' but YB_COMPILER_TYPE is '%s'" % (
                    build_root, compiler_type, compiler_type_from_env))
    from yb import common_util
    os.environ['YB_COMPILER_TYPE'] = compiler_type

    global global_conf
    global_conf = GlobalTestConfig(
            build_root=build_root,
            build_type=build_type,
            yb_src_root=yb_src_root,
            archive_for_workers=archive_for_workers,
            rel_build_root=rel_build_root,
            compiler_type=compiler_type,
            # The archive might not even exist yet.
            archive_sha256sum=None)
    return global_conf


def set_global_conf_from_dict(global_conf_dict: Dict[str, str]) -> GlobalTestConfig:
    """
    This is used in functions that run on Spark. We use a dictionary to pass the configuration from
    the main program to distributed workers.
    """
    global global_conf
    try:
        global_conf = GlobalTestConfig(**global_conf_dict)
    except Exception as ex:
        logging.exception("Cannot set global configuration from dictionary %s" % global_conf_dict)
        raise ex

    return global_conf


# -------------------------------------------------------------------------------------------------
# Archive generation for running tests on Spark workers
# -------------------------------------------------------------------------------------------------

ARCHIVED_PATHS_IN_BUILD_DIR = [
    'bin',
    'ent',
    'lib',
    'postgres',
    'share',
    'auto_flags.json',
    'version_metadata.json',
    'linuxbrew_path.txt',
    'thirdparty_path.txt',
    'thirdparty_url.txt',
    'postgres_build/contrib',
    'postgres_build/src/test/regress',
    'postgres_build/src/test/isolation',
    'postgres_build/src/include/catalog/pg_yb_migration.dat',  # used by TestYsqlUpgrade
]

ARCHIVED_PATHS_IN_SRC_DIR = [
    'bin',
    'build-support',
    'managed/devops/bin/yb_backup.py',
    'managed/src/main/resources/version.txt',
    'managed/version.txt',
    'python',
    'submodules',
    'version.txt',
    'www',
    'yb_build.sh',
    'build/venv',
    'requirements.txt',
    'requirements_frozen.txt',
    'build/yugabyte-bash-common',
]


def find_rel_java_paths_to_archive(yb_src_root: str) -> List[str]:
    paths = []
    for ent in [False, True]:
        path_components = []
        if ent:
            path_components.append('ent')
        path_components.append('java')
        java_dir_path = os.path.join(yb_src_root, *path_components)
        paths.append(os.path.join(java_dir_path, 'pom.xml'))
        for submodule_dir_path in glob.glob(os.path.join(java_dir_path, '*')):
            for name in ['pom.xml', 'src']:
                paths.append(os.path.join(submodule_dir_path, name))
            for classes_dir_name in ['classes', 'test-classes']:
                paths.append(os.path.join(submodule_dir_path, 'target', classes_dir_name))
    return [os.path.relpath(p, yb_src_root) for p in paths]


def validate_mvn_local_repo(mvn_local_repo: str) -> None:
    """
    Check the presence of some required artifacts.
    """
    found_errors = False
    for rel_path_pattern in [
        'org/apache/maven/plugins/maven-antrun-plugin/*/maven-antrun-plugin',
        'org/apache/maven/plugins/maven-assembly-plugin/*/maven-assembly-plugin',
        'org/apache/maven/plugins/maven-clean-plugin/*/maven-clean-plugin',
        'org/apache/maven/plugins/maven-compiler-plugin/*/maven-compiler-plugin',
        'org/apache/maven/plugins/maven-dependency-plugin/*/maven-dependency-plugin',
        'org/apache/maven/plugins/maven-deploy-plugin/*/maven-deploy-plugin',
        'org/apache/maven/plugins/maven-enforcer-plugin/*/maven-enforcer-plugin',
        'org/apache/maven/plugins/maven-install-plugin/*/maven-install-plugin',
        'org/apache/maven/plugins/maven-jar-plugin/*/maven-jar-plugin',
        'org/apache/maven/plugins/maven-javadoc-plugin/*/maven-javadoc-plugin',
        'org/apache/maven/plugins/maven-resources-plugin/*/maven-resources-plugin',
        'org/apache/maven/plugins/maven-site-plugin/*/maven-site-plugin',
        'org/apache/maven/plugins/maven-source-plugin/*/maven-source-plugin',
        'org/apache/maven/plugins/maven-surefire-plugin/*/maven-surefire-plugin',
        'org/xolstice/maven/plugins/protobuf-maven-plugin/*/protobuf-maven-plugin',
    ]:
        for suffix in ['.pom', '.jar']:
            glob_pattern = os.path.join(mvn_local_repo, f"{rel_path_pattern}-*{suffix}")
            glob_result = glob.glob(glob_pattern)
            if not glob_result:
                logging.warning(f"Glob pattern did not return any results: {glob_pattern}.")
                found_errors = True
    if found_errors:
        logging.info(
            "The above warnings about glob patterns mean that Java tests could fail to run "
            f"properly on Spark. Maven local repo: {mvn_local_repo}")
    else:
        logging.info(f"All Maven plugin patterns were found in local repo {mvn_local_repo}")


def create_archive_for_workers() -> None:
    assert global_conf is not None
    dest_path = global_conf.archive_for_workers
    if dest_path is None:
        return
    tmp_dest_path = '%s.tmp.%d' % (dest_path, random.randint(0, 2 ** 64 - 1))

    start_time_sec = time.time()
    try:
        build_root = os.path.abspath(global_conf.build_root)
        compiler_type = get_compiler_type_from_build_root(build_root)
        yb_src_root = os.path.abspath(global_conf.yb_src_root)
        build_root_parent = os.path.join(yb_src_root, 'build')
        rel_build_root = global_conf.rel_build_root
        if os.path.exists(dest_path):
            logging.info("Removing existing archive file %s", dest_path)
            os.remove(dest_path)
        paths_in_src_dir = ARCHIVED_PATHS_IN_SRC_DIR + find_rel_java_paths_to_archive(yb_src_root)

        added_local_repo = False
        mvn_local_repo = os.environ.get('YB_MVN_LOCAL_REPO')
        if mvn_local_repo:
            mvn_local_repo = os.path.abspath(mvn_local_repo)
            if mvn_local_repo.startswith(build_root_parent + '/'):
                # Here, the path we're adding has to be relative to YB_SRC_ROOT.
                paths_in_src_dir.append(os.path.relpath(mvn_local_repo, yb_src_root))
                logging.info("Will add YB_MVN_LOCAL_REPO to archive: %s", mvn_local_repo)
                validate_mvn_local_repo(mvn_local_repo)
                added_local_repo = True
        if not added_local_repo:
            raise ValueError("YB_MVN_LOCAL_REPO (%s) must be within $YB_SRC_ROOT/build (%s)" % (
                mvn_local_repo, build_root_parent))

        files_that_must_exist_in_build_dir = ['thirdparty_path.txt']

        # This will not include version-specific compiler types like clang11 or gcc9.
        # We will eventually get rid of Linuxbrew and simplify this.
        if sys.platform == 'linux' and compiler_type in ['gcc', 'clang']:
            files_that_must_exist_in_build_dir.append('linuxbrew_path.txt')

        for rel_file_path in files_that_must_exist_in_build_dir:
            full_path = os.path.join(build_root, rel_file_path)
            if not os.path.exists(full_path):
                raise IOError("Path does not exist: %s" % full_path)

        # TODO: save the list of files added to the archive to a separate file for debuggability.
        # TODO: use zip instead of tar/gz.
        tar_args = [
            'tar',
            'cz',
            '-f',
            tmp_dest_path
        ] + [
            path_rel_to_src_dir
            for path_rel_to_src_dir in paths_in_src_dir
            if os.path.exists(os.path.join(yb_src_root, path_rel_to_src_dir))
        ] + [
            os.path.join(rel_build_root, path_rel_to_build_root)
            for path_rel_to_build_root in ARCHIVED_PATHS_IN_BUILD_DIR
            if os.path.exists(os.path.join(build_root, path_rel_to_build_root))
        ] + [
            os.path.relpath(test_program_path, yb_src_root)
            for test_program_path in glob.glob(os.path.join(build_root, 'tests-*'))
            if os.path.exists(test_program_path)
        ]

        logging.info("Running the tar command: %s", tar_args)
        subprocess.check_call(tar_args, cwd=global_conf.yb_src_root)
        if not os.path.exists(tmp_dest_path):
            raise IOError(
                    "Archive '%s' did not get created after command %s" % (
                        tmp_dest_path, tar_args))
        os.rename(tmp_dest_path, dest_path)
        logging.info("Size of the archive: %.1f MiB", os.path.getsize(dest_path) / (1024.0 * 1024))
    finally:
        elapsed_time_sec = time.time() - start_time_sec
        logging.info("Elapsed archive creation time: %.1f seconds", elapsed_time_sec)
        if os.path.exists(tmp_dest_path):
            logging.warning("Removing unfinished temporary archive file %s", tmp_dest_path)
            os.remove(tmp_dest_path)


# These SHA256-related functions are duplicated in download_and_extract_archive.py, because that
# script should not depend on any Python modules.

def validate_sha256sum(checksum_str: str) -> None:
    if not re.match(r'^[0-9a-f]{64}$', checksum_str):
        raise ValueError("Invalid SHA256 checksum: '%s', expected 64 hex characters", checksum_str)


def compute_sha256sum(file_path: str) -> str:
    cmd_line = None
    if sys.platform.startswith('linux'):
        cmd_line = ['sha256sum', file_path]
    elif sys.platform.startswith('darwin'):
        cmd_line = ['shasum', '--algorithm', '256', file_path]
    else:
        raise ValueError("Don't know how to compute SHA256 checksum on platform %s" % sys.platform)

    checksum_str = subprocess.check_output(cmd_line).strip().split()[0].decode('utf-8')
    validate_sha256sum(checksum_str)
    return checksum_str


def compute_archive_sha256sum() -> None:
    assert global_conf is not None
    if global_conf.archive_for_workers is not None:
        global_conf.archive_sha256sum = compute_sha256sum(global_conf.archive_for_workers)
        logging.info("SHA256 checksum of archive %s: %s" % (
            global_conf.archive_for_workers, global_conf.archive_sha256sum))


def to_real_nfs_path(path: str) -> str:
    assert path.startswith('/'), "Expecting the path to be absolute: %s" % path
    path = os.path.abspath(path)
    return '/real_%s' % path[1:]


def get_tmp_filename(prefix: str = '', suffix: str = '', auto_remove: bool = False) -> str:
    fd, file_path = tempfile.mkstemp(prefix=prefix, suffix=suffix)
    os.close(fd)
    os.remove(file_path)
    if auto_remove:
        def cleanup() -> None:
            if os.path.exists(file_path):
                os.remove(file_path)
        atexit.register(cleanup)
    return file_path


def get_global_conf() -> GlobalTestConfig:
    assert global_conf is not None
    return global_conf
