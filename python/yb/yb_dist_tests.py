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
import command_util
import copy
import logging
import os
import re
import time

from yb.common_util import get_build_type_from_build_root, is_macos  # nopep8


# This is used to separate relative binary path from gtest_filter for C++ tests in what we call
# a "test descriptor" (a string that identifies a particular test).
#
# This must match the constant with the same name in common-test-env.sh.
TEST_DESCRIPTOR_SEPARATOR = ":::"

JAVA_TEST_DESCRIPTOR_RE = re.compile(r'^([a-z0-9-]+)/src/test/(?:java|scala)/(.*)$')

TEST_DESCRIPTOR_ATTEMPT_PREFIX = TEST_DESCRIPTOR_SEPARATOR + 'attempt_'
TEST_DESCRIPTOR_ATTEMPT_INDEX_RE = re.compile(
    r'^(.*)' + TEST_DESCRIPTOR_ATTEMPT_PREFIX + r'(\d+)$')

global_conf = None

CLOCK_SYNC_WAIT_LOGGING_INTERVAL_SEC = 10

MAX_TIME_TO_WAIT_FOR_CLOCK_SYNC_SEC = 60


class TestDescriptor:
    """
    A "test descriptor" identifies a particular test we could run on a distributed test worker.
    A string representation of a "test descriptor" is one of the following:
    - A C++ test program name relative to the build root. This implies running all tests within
      the test program. This has the disadvantage that a failure of one of those tests will cause
      the rest of tests to not be run.
    - A C++ test program name relative to the build root followed by the ':::' separator and the
      gtest filter identifying a test within that test program,
    - A string like 'com.yugabyte.jedis.TestYBJedis#testPool[1]' describing a Java test. This is
      something that could be passed directly to the -Dtest=... Maven option.
    - A Java test class source path (including .java/.scala extension) relative to the "java"
      directory in the YugaByte DB source tree.
    """
    def __init__(self, descriptor_str):
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
                mvn_module, package_and_class_with_slashes = JAVA_TEST_DESCRIPTOR_RE.match(
                    self.descriptor_str_without_attempt_index).groups()

                package_and_class = package_and_class_with_slashes.replace('/', '.')
                self.args_for_run_test = "{} {}".format(mvn_module, package_and_class)
                output_file_name = package_and_class
        else:
            self.language = 'C++'
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
                output_file_name += '__' + test_name

        output_file_name = re.sub(r'[\[\]/#]', '_', output_file_name)
        self.error_output_path = os.path.join(
                global_conf.build_root, 'yb-test-logs', output_file_name + '__error.log')

    def __str__(self):
        if self.attempt_index == 1:
            return self.descriptor_str_without_attempt_index
        return "{}{}{}".format[
            self.descriptor_str_without_attempt_index,
            TEST_DESCRIPTOR_ATTEMPT_PREFIX,
            self.attempt_index
            ]

    def with_attempt_index(self, attempt_index):
        assert attempt_index >= 1
        copied = copy.copy(self)
        copied.attempt_index = attempt_index
        # descriptor_str is just the cached version of the string representation, with the
        # attempt_index included (if it is greater than 1).
        copied.descriptor_str = str(copied)
        return copied


class GlobalTestConfig:
    def __init__(self, build_root, build_type, yb_src_root):
        self.build_root = build_root
        self.build_type = build_type
        self.yb_src_root = yb_src_root

    def get_run_test_script_path(self):
        return os.path.join(self.yb_src_root, 'build-support', 'run-test.sh')

    def set_env(self, propagated_env_vars={}):
        """
        Used on the distributed worker side (inside functions that run on Spark) to configure the
        necessary environment.
        """
        os.environ['BUILD_ROOT'] = self.build_root
        # This is how we tell run-test.sh what set of C++ binaries to use for mini-clusters in Java
        # tests.
        for env_var_name, env_var_value in propagated_env_vars.iteritems():
            os.environ[env_var_name] = env_var_value


TestResult = collections.namedtuple(
        'TestResult',
        ['test_descriptor',
         'exit_code',
         'elapsed_time_sec',
         'failed_without_output'])

ClockSyncCheckResult = collections.namedtuple(
        'ClockSyncCheckResult',
        ['is_synchronized',
         'cmd_result'])


def set_global_conf_from_args(args):
    build_root = os.path.realpath(args.build_root)

    # This module is expected to be under python/yb.
    yb_src_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

    # Ensure that build_root is consistent with yb_src_root above.
    yb_src_root_from_build_root = os.path.dirname(os.path.dirname(build_root))

    build_type = get_build_type_from_build_root(build_root)

    assert yb_src_root == yb_src_root_from_build_root, \
        ("An inconstency between YB_SRC_ROOT derived from module location ({}) vs. the one derived "
         "from BUILD_ROOT ({})").format(yb_src_root, yb_src_root_from_build_root)
    global global_conf
    global_conf = GlobalTestConfig(
            build_root=build_root,
            build_type=build_type,
            yb_src_root=yb_src_root)
    return global_conf


def set_global_conf_from_dict(global_conf_dict):
    """
    This is used in functions that run on Spark. We use a dictionary to pass the configuration from
    the main program to distributed workers.
    """
    global global_conf
    try:
        global_conf = GlobalTestConfig(**global_conf_dict)
    except TypeError as ex:
        raise TypeError("Cannot set global configuration from dictionary %s: %s" % (
            repr(global_conf_dict), ex.message))

    return global_conf


def is_clock_synchronized():
    assert not is_macos()
    result = command_util.run_program('ntpstat', error_ok=True)
    return ClockSyncCheckResult(
        is_synchronized=result.stdout.startswith('synchron'),
        cmd_result=result)


def wait_for_clock_sync():
    if is_macos():
        return

    start_time = time.time()
    last_log_time = start_time
    waited_for_clock_sync = False
    check_result = is_clock_synchronized()
    while not check_result.is_synchronized:
        if not waited_for_clock_sync:
            logging.info("Clock not synchronized, waiting...")
            waited_for_clock_sync = True
        time.sleep(0.25)
        cur_time = time.time()
        if cur_time - last_log_time > CLOCK_SYNC_WAIT_LOGGING_INTERVAL_SEC:
            logging.info("Waiting for clock to be synchronized for %.2f sec" %
                         (cur_time - last_log_time))
            last_log_time = cur_time
        if cur_time - start_time > MAX_TIME_TO_WAIT_FOR_CLOCK_SYNC_SEC:
            raise RuntimeError(
                "Waited for %.2f sec for clock synchronization, still not synchronized, "
                "check result: %s" % (cur_time - start_time, check_result))
        check_result = is_clock_synchronized()

    if waited_for_clock_sync:
        cur_time = time.time()
        logging.info("Waited for %.2f for clock synchronization" % (cur_time - start_time))
