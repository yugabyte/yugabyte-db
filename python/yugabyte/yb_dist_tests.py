# Copyright (c) YugabyteDB, Inc.
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

from typing import Optional, List, Set, Dict

from yugabyte.common_util import (
    get_build_type_from_build_root,
    get_compiler_type_from_build_root
)
from yugabyte.command_util import has_pigz
from yugabyte.postgres_build_util import POSTGRES_BUILD_SUBDIR
from yugabyte import artifact_upload
from yugabyte.test_descriptor import TestDescriptor

import dataclasses


# Name of the worker archive inside a build root. The .spark-no-extract suffix keeps Spark's
# addFile from unpacking it for us - the untar step on the worker controls where the tree lands.
ARCHIVE_FOR_WORKERS_NAME = 'archive_for_tests_on_spark.tar.gz.spark-no-extract'

CLOCK_SYNC_WAIT_LOGGING_INTERVAL_SEC = 10

MAX_TIME_TO_WAIT_FOR_CLOCK_SYNC_SEC = 60


class TestConfig:
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


@dataclasses.dataclass
class TestResult:
    test_descriptor: TestDescriptor
    exit_code: int
    elapsed_time_sec: float
    failed_without_output: bool

    # Paths of artifacts relative to the source root.
    artifact_paths: Optional[List[str]]

    artifact_copy_result: Optional[artifact_upload.FileTransferResult]
    spark_error_copy_result: Optional[artifact_upload.FileTransferResult]

    def log_artifact_upload_errors(self) -> None:
        for copy_result in [self.artifact_copy_result, self.spark_error_copy_result]:
            if copy_result is not None and copy_result.has_errors():
                logging.info("Had errors during artifact upload: %s", copy_result)


# Derives a conf from a build root alone: everything about a tree that follows from its
# <yb_src_root>/build/<flavor> layout.
def make_conf_for_build_root(
        build_root: str, send_archive_to_workers: bool, archive_name: str) -> TestConfig:
    build_root = os.path.realpath(build_root)
    yb_src_root = os.path.dirname(os.path.dirname(build_root))

    archive_for_workers = None
    if send_archive_to_workers:
        archive_for_workers = os.path.abspath(os.path.join(build_root, archive_name))

    rel_build_root = os.path.relpath(
            os.path.abspath(build_root),
            os.path.abspath(yb_src_root))
    if len(rel_build_root.split('/')) != 2:
        raise ValueError(
                "Unexpected number of components in the relative path of build root to "
                "source root: %s. build_root=%s, yb_src_root=%s" % (
                    rel_build_root, build_root, yb_src_root))

    return TestConfig(
            build_root=build_root,
            build_type=get_build_type_from_build_root(build_root),
            yb_src_root=yb_src_root,
            archive_for_workers=archive_for_workers,
            rel_build_root=rel_build_root,
            compiler_type=get_compiler_type_from_build_root(build_root),
            # The archive might not even exist yet.
            archive_sha256sum=None)


# The conf for this checkout, from the command line. Also settles $YB_COMPILER_TYPE, which the build
# scripts read, so it has to run before anything shells out.
def conf_from_args(args: argparse.Namespace) -> TestConfig:
    conf = make_conf_for_build_root(
        args.build_root, args.send_archive_to_workers, ARCHIVE_FOR_WORKERS_NAME)

    # This module is expected to be under python/yugabyte. Unlike the rest of the conf, this is a
    # statement about the tree we are running *from*, so it only holds for this checkout.
    yb_src_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
    assert yb_src_root == conf.yb_src_root, \
        ("An inconstency between YB_SRC_ROOT derived from module location ({}) vs. the one derived "
         "from BUILD_ROOT ({})").format(yb_src_root, conf.yb_src_root)

    compiler_type_from_env = os.environ.get('YB_COMPILER_TYPE')
    if compiler_type_from_env is not None and compiler_type_from_env != conf.compiler_type:
        raise ValueError(
                "Build root '%s' implies compiler type '%s' but YB_COMPILER_TYPE is '%s'" % (
                    conf.build_root, conf.compiler_type, compiler_type_from_env))
    os.environ['YB_COMPILER_TYPE'] = conf.compiler_type

    return conf


# -------------------------------------------------------------------------------------------------
# Archive generation for running tests on Spark workers
# -------------------------------------------------------------------------------------------------

ARCHIVED_PATHS_IN_BUILD_DIR = [
    'bin',
    'gobin',
    'lib',
    'openssl-config',
    'postgres',
    'share',
    'test_certs',
    'auto_flags.json',
    'master_flags.xml',
    'tserver_flags.xml',
    'version_metadata.json',
    'thirdparty_path.txt',
    'thirdparty_url.txt',
    'upgrade_test_builds',
    'gflag_allowlist.txt',
    'test_xcluster_ddl_replication_sql',
    'test_conflict_resolve_keys_verification_sql',
    f'{POSTGRES_BUILD_SUBDIR}/contrib',
    f'{POSTGRES_BUILD_SUBDIR}/src/test/modules',
    f'{POSTGRES_BUILD_SUBDIR}/src/test/regress',
    f'{POSTGRES_BUILD_SUBDIR}/src/test/isolation',
    f'{POSTGRES_BUILD_SUBDIR}/third-party-extensions',
    f'{POSTGRES_BUILD_SUBDIR}/yb-extensions',

    # Used by TestYsqlUpgrade.
    f'{POSTGRES_BUILD_SUBDIR}/src/include/catalog/pg_yb_migration.dat',
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
    'build/venv-arm64',
    'build/ybc',
    'build/requirements.txt',
    'build/requirements_frozen.txt',
    'requirements.txt',
    'requirements_frozen.txt',
    'build/yugabyte-bash-common',
    'yb.env'
]


def find_rel_java_paths_to_archive(yb_src_root: str) -> List[str]:
    paths = []
    java_dir_path = os.path.join(yb_src_root, 'java')
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


# Packs the worker archive for the build tree described by conf, including mvn_local_repo as that
# tree's Maven repository. Java tests run `mvn --offline` on the worker, so the repo has to be
# included into the archive. Raises an error if mvn_local_repo is outside of build subdirectory.
def create_archive_for_workers(conf: TestConfig, mvn_local_repo: str) -> None:
    dest_path = conf.archive_for_workers
    if dest_path is None:
        return
    tmp_dest_path = '%s.tmp.%d' % (dest_path, random.randint(0, 2 ** 64 - 1))

    start_time_sec = time.time()
    try:
        build_root = os.path.abspath(conf.build_root)
        compiler_type = get_compiler_type_from_build_root(build_root)
        yb_src_root = os.path.abspath(conf.yb_src_root)
        build_root_parent = os.path.join(yb_src_root, 'build')
        rel_build_root = conf.rel_build_root
        if os.path.exists(dest_path):
            logging.info("Removing existing archive file %s", dest_path)
            os.remove(dest_path)
        paths_in_src_dir = ARCHIVED_PATHS_IN_SRC_DIR + find_rel_java_paths_to_archive(yb_src_root)

        mvn_local_repo = os.path.abspath(mvn_local_repo)
        if not mvn_local_repo.startswith(build_root_parent + '/'):
            raise ValueError("Maven local repo (%s) must be within <yb_src_root>/build (%s)" % (
                mvn_local_repo, build_root_parent))
        paths_in_src_dir.append(os.path.relpath(mvn_local_repo, yb_src_root))
        logging.info("Will add Maven local repo to archive: %s", mvn_local_repo)
        validate_mvn_local_repo(mvn_local_repo)

        files_that_must_exist_in_build_dir = ['thirdparty_path.txt']

        for rel_file_path in files_that_must_exist_in_build_dir:
            full_path = os.path.join(build_root, rel_file_path)
            if not os.path.exists(full_path):
                raise IOError("Path does not exist: %s" % full_path)

        # TODO: save the list of files added to the archive to a separate file for debuggability.
        # TODO: use zip instead of tar/gz.
        tar_args = [
            'tar',
        ] + (['-I', 'pigz'] if has_pigz() else ['-z']) + [
            '-c',
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
        subprocess.check_call(tar_args, cwd=yb_src_root)
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


def compute_archive_sha256sum(conf: TestConfig) -> None:
    if conf.archive_for_workers is not None:
        conf.archive_sha256sum = compute_sha256sum(conf.archive_for_workers)
        logging.info("SHA256 checksum of archive %s: %s" % (
            conf.archive_for_workers, conf.archive_sha256sum))


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
