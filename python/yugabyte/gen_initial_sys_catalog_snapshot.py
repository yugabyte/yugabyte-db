#!/usr/bin/env python

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

"""
A wrapper script for a C++ program that generates the initial sys catalog snapshot and the YSQL
major upgrade initdb seed.
"""

import sys
import os
import subprocess
import logging
import shutil
import time

from yugabyte_pycommon import run_program, WorkDirContext, mkdir_p  # type: ignore

from yugabyte.common_util import YB_SRC_ROOT, init_logging


def create(build_root: str,
           description: str,
           log_prefix: str,
           dest_path: str,
           file_to_check: str,
           dest_flag: str) -> None:
    tool_name = 'create_initial_sys_catalog_snapshot'
    tool_path = os.path.join(build_root, 'tests-pgwrapper', tool_name)

    if (os.path.exists(file_to_check) and
            os.environ.get('YB_RECREATE_INITIAL_SYS_CATALOG_SNAPSHOT', '') != '1'):
        logging.info("%s already exists, not re-creating: %s", description, dest_path)
        return
    if os.path.exists(dest_path):
        logging.info("Removing %s at: %s", description, dest_path)
        shutil.rmtree(dest_path)

    mkdir_p(os.path.dirname(dest_path))

    start_time_sec = time.time()
    logging.info("Starting creating %s", description)
    logging.info("Logging to: %s", os.path.join(build_root, log_prefix + '.err'))
    os.environ['YB_EXTRA_GTEST_FLAGS'] = ' '.join(
        ['--%s=%s' % (dest_flag, dest_path)] + sys.argv[1:])
    os.environ['YB_CTEST_VERBOSE'] = '1'
    # A filter inherited from yb_build.sh --gtest_filter would match no tests in this tool.
    os.environ.pop('YB_GTEST_FILTER', None)
    with WorkDirContext(build_root):
        initdb_result = run_program(
            [
                os.path.join(YB_SRC_ROOT, 'build-support', 'run-test.sh'),
                tool_path,
            ],
            stdout_stderr_prefix=log_prefix,
            shell=True,
            error_ok=True
        )
        elapsed_time_sec = time.time() - start_time_sec
        if initdb_result.failure():
            initdb_result.print_output_to_stdout()
            raise RuntimeError("initdb failed in %.1f sec" % elapsed_time_sec)

    logging.info(
        "Creation of %s took %1.f sec. Wrote data to: %s",
        description, elapsed_time_sec, dest_path)


def main() -> None:
    if os.environ.get('YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT', '0') == '1':
        logging.info('YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT is set, skipping initdb')
        return

    build_root = os.environ['YB_BUILD_ROOT']

    snapshot_dest_path = os.path.join(build_root, 'share', 'initial_sys_catalog_snapshot')
    create(build_root,
           description='initial sys catalog snapshot',
           log_prefix='create_initial_sys_catalog_snapshot',
           dest_path=snapshot_dest_path,
           file_to_check=os.path.join(snapshot_dest_path, 'exported_tablet_metadata_changes'),
           dest_flag='initial_sys_catalog_snapshot_dest_path')

    seed_dest_path = os.path.join(build_root, 'share', 'ysql_major_upgrade_initdb_seed')
    create(build_root,
           description='YSQL major upgrade initdb seed',
           log_prefix='create_ysql_major_upgrade_initdb_seed',
           dest_path=seed_dest_path,
           file_to_check=os.path.join(seed_dest_path, 'template1_catalog'),
           dest_flag='ysql_major_upgrade_initdb_seed_dest_path')


if __name__ == '__main__':
    init_logging(verbose=False)
    main()
