#!/usr/bin/env python

"""
Run tests on Spark using PySpark.
"""

import argparse
import glob
import logging
import os
import re
import socket
import sys
import time


JAVA_TEST_DESCRIPTOR_RE = re.compile(r'^([a-z0-9-]+)/src/test/(?:java|scala)/(.*)$')

# This is useful when debugging. Enabling this will keep this Spark application running after
# the Spark job has finished running, and the application UI can be accessed by following a link
# under "Running Applications" on http://buildmaster:8080/.
SLEEP_AFTER_TESTS_DONE = False

PROPAGATED_ENV_VARS = [
        'BUILD_ID',
        'BUILD_URL',
        'JOB_NAME',
        ]

# This is used to separate relative binary path from gtest_filter for C++ tests in what we call
# a "test descriptor" (a string that identifies a particular test).
#
# This must match the constant with the same name in common-test-env.sh.
TEST_DESCRIPTOR_SEPARATOR = ":::"

# This directory inside $BUILD_ROOT contains files listing all C++ tests (one file per test
# program).
#
# This must match the constant with the same name in common-test-env.sh.
LIST_OF_TESTS_DIR_NAME = 'list_of_tests'

# Global variables.
yb_src_root = None
build_root = None
mvn_local_repo = None
propagated_env_vars = {}


def run_test(test_descriptor):
    """
    test_descriptor is either a C++ test program name relative to the build root, optionally
    followed by the separator and the gtest filter identifying a test within that test program,
    or a Java test class source pathn (including .java/.scala extension) relative to the "java"
    directory.
    """
    # This is how we tell run-test.sh what set of C++ binaries to use for mini-clusters in Java
    # tests.
    os.environ['BUILD_ROOT'] = build_root
    os.environ['YB_MVN_LOCAL_REPO'] = mvn_local_repo
    for env_var_name, env_var_value in propagated_env_vars.iteritems():
        os.environ[env_var_name] = env_var_value

    if test_descriptor.endswith('.java') or test_descriptor.endswith('.scala'):
        # This is a Java test. The "test descriptor" is the Java source file path relative to the
        # "java" directory.
        mvn_module, package_and_class_with_slashes = \
                JAVA_TEST_DESCRIPTOR_RE.match(test_descriptor).groups()

        run_test_args = "{} {}".format(mvn_module,
                                       package_and_class_with_slashes.replace('/', '.'))
    else:
        if TEST_DESCRIPTOR_SEPARATOR in test_descriptor:
            rel_test_binary, test_name = test_descriptor.split(TEST_DESCRIPTOR_SEPARATOR)
        else:
            rel_test_binary = test_descriptor
            test_name = None

        # Arguments for run-test.sh.
        # - The absolute path to the test binary (the test descriptor only contains the relative
        #   path).
        # - Optionally, the gtest filter within the test program.
        run_test_args = os.path.join(build_root, rel_test_binary)
        if test_name:
            run_test_args += " " + test_name

    rc = os.system(
        ("bash -c 'build_type={} " +
         "{}/build-support/run-test.sh {}'").format(
            build_type,
            yb_src_root,
            run_test_args)) >> 8

    logging.info("Test {} ran on {}, rc={}".format(test_descriptor, socket.gethostname(), rc))
    return rc


def set_build_parameters(args):
    global build_root, build_type, yb_src_root
    build_type = args.build_type
    build_root = args.build_root

    # This script is expected to be in build-support, one level below YB_SRC_ROOT.
    yb_src_root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

    # Ensure that build_root is consistent with yb_src_root above.
    yb_src_root_from_build_root = os.path.dirname(os.path.dirname(build_root))
    assert yb_src_root == yb_src_root_from_build_root, \
        "Inconsistent YB_SRC_ROOT from script location ({}) vs. BUILD_ROOT ({})".format(
            yb_src_root, yb_src_root_from_build_root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Run tests on Spark.')
    parser.add_argument('--verbose', dest='verbose', action='store_true',
                        help='Enable debug output')
    parser.add_argument('--java', dest='run_java_tests', action='store_true',
                        help='Run Java tests')
    parser.add_argument('--cpp', dest='run_cpp_tests', action='store_true',
                        help='Run C++ tests')
    parser.add_argument('--build-root', dest='build_root', required=True,
                        help='Build root (e.g. ~/code/yugabyte/build/debug-gcc-dynamic)')
    parser.add_argument('--build-type', dest='build_type', required=True,
                        help='Build type (e.g. debug, release, tsan, or asan)')
    args = parser.parse_args()

    log_level = logging.INFO
    if args.verbose:
        log_level = logging.DEBUG
    logging.basicConfig(
        level=logging.INFO,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    set_build_parameters(args)
    if not args.run_cpp_tests and not args.run_java_tests:
        logging.error("At least one of --java or --cpp has to be specified")
        sys.exit(1)

    cpp_test_descriptors = []
    if args.run_cpp_tests:
        for test_list_path in glob.glob(os.path.join(build_root,
                                                     LIST_OF_TESTS_DIR_NAME,
                                                     '*.test_list')):
            cpp_test_descriptors += [
                    s.strip() for s in open(test_list_path).readlines() if s.strip()]

    global mvn_local_repo
    java_test_descriptors = []
    if args.run_java_tests:
        java_src_root = os.path.join(yb_src_root, 'java')
        for dir_path, dir_names, file_names in os.walk(java_src_root):
            rel_dir_path = os.path.relpath(dir_path, java_src_root)
            for file_name in file_names:
                if (file_name.startswith('Test') and
                    (file_name.endswith('.java') or file_name.endswith('.scala')) or
                    file_name.endswith('Test.java') or file_name.endswith('Test.scala')) \
                            and '/src/test/' in rel_dir_path:
                    test_descriptor = os.path.join(rel_dir_path, file_name)
                    if JAVA_TEST_DESCRIPTOR_RE.match(test_descriptor):
                        java_test_descriptors.append(test_descriptor)
                    else:
                        logging.warning("Skipping file (does not match expected pattern): " +
                                        test_descriptor)
        mvn_local_repo = os.environ.get(
                'YB_MVN_LOCAL_REPO',
                os.path.join(os.path.expanduser('~'), '.m2', 'repository'))

    # TODO: sort tests in the order of reverse historical execution time, if Spark starts running
    # tasks from the beginning -- that will ensure the longest tests start the earliest. We'll need
    # to maintain execution time stats for that.
    #
    # Right now we just put Java tests first because those tests are entire test classes and will
    # take longer to run on average.
    test_descriptors = sorted(java_test_descriptors) + sorted(cpp_test_descriptors)
    if args.verbose:
        for test_descriptor in test_descriptors:
            logging.debug("Will run test: {}".format(test_descriptor))

    for env_var_name in PROPAGATED_ENV_VARS:
        if env_var_name in os.environ:
            propagated_env_vars[env_var_name] = os.environ[env_var_name]

    from pyspark import SparkContext
    num_tests = len(test_descriptors)
    logging.info("Running {} tests on Spark".format(num_tests))
    sc = SparkContext("spark://buildmaster.c.yugabyte.internal:7077", "Distributed Tests")

    test_names_rdd = sc.parallelize(test_descriptors, numSlices=num_tests)
    results = test_names_rdd.map(run_test)

    exit_codes = set(results.collect())

    if exit_codes == set([0]):
        global_exit_code = 0
    else:
        global_exit_code = 1

    logging.info("Tests are done, set of exit codes: {}, will return exit code {}".format(
        sorted(exit_codes), global_exit_code))

    if SLEEP_AFTER_TESTS_DONE:
        time.sleep(300)

    sys.exit(global_exit_code)
