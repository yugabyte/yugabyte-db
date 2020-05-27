#!/usr/bin/env python

"""
Post-processes results of running a single YugabyteDB unit test (e.g. a C++ or Java test) and
creates a structured output file with a summary of those results. This includes test running time
and possible causes of test failure.
"""

import sys
import os
import logging
import yugabyte_pycommon
import argparse
import xml.etree.ElementTree as ET
import json
import signal
import glob

# Example test failure (from C++)
# <?xml version="1.0" ?><testsuites disabled="0" errors="0" failures="1" name="AllTests" tests="1
#                                   time="0.125" timestamp="2019-03-07T23:04:23">
#   <testsuite disabled="0" errors="0" failures="1" name="StringUtilTest" tests="1" time="0.125">
#     <testcase classname="StringUtilTest" name="TestCollectionToString" status="run" time="0.125">
#       <failure message="../../src/yb/gutil/strings/string_util-test.cc:110
# Failed" type="">
# <![CDATA[../../src/yb/gutil/strings/string_util-test.cc:110
# Failed]]></failure>
#     </testcase>
#   </testsuite>
# </testsuites>

# Example test success (Java):
# <?xml version="1.0" encoding="UTF-8"?>
# <testsuite name="org.yb.cql.SimpleQueryTest" time="13.539" tests="1" errors="0" skipped="0"
#            failures="0">
#   <properties>
#     <!-- ... -->
#   </properties>
#   <testcase name="restrictionOnRegularColumnWithStaticColumnPresentTest"
#             classname="org.yb.cql.SimpleQueryTest" time="12.347"/>
# </testsuite>

# Example test failure (Java):
# <?xml version="1.0" encoding="UTF-8"?>
# <testsuite name="com.yugabyte.jedis.TestReadFromFollowers"
#            time="11.383" tests="1" errors="1" skipped="0" failures="0">
#   <properties>
#     <!-- ... -->
#   </properties>
#   <testcase name="testSameZoneOps[1]" classname="com.yugabyte.jedis.TestReadFromFollowers"
#             time="11.209">
#     <error message="Could not get a resource from the pool"
#            type="redis.clients.jedis.exceptions.JedisConnectionException">
#       redis.clients.jedis.exceptions.JedisConnectionException: Could not get a resource from the
#       pool
#     </error>
#     <system-out>...</system-out>
#   </testcase>
# </testsuite>

# Other related resources:
# https://llg.cubic.org/docs/junit/
# https://stackoverflow.com/questions/442556/spec-for-junit-xml-output/4926073#4926073
# https://raw.githubusercontent.com/windyroad/JUnit-Schema/master/JUnit.xsd


def rename_key(d, key, new_key):
    if key in d:
        d[new_key] = d[key]
        del d[key]


def del_default_value(d, key, default_value):
    if key in d and d[key] == default_value:
        del d[key]


def count_subelements(root, el_name):
    return len(list(root.iter(el_name)))


def set_element_count_property(test_kvs, root, el_name, field_name, increment_by=0):
    count = count_subelements(root, el_name) + increment_by
    if count != 0:
        test_kvs[field_name] = count


class Postprocessor:

    def __init__(self):
        parser = argparse.ArgumentParser(
            description=__doc__)
        parser.add_argument(
            '--yb-src-root',
            help='Root directory of YugaByte source code',
            required=True)
        parser.add_argument(
            '--build-root',
            help='Root directory of YugaByte build',
            required=True)
        parser.add_argument(
            '--test-log-path',
            help='Main log file of this test',
            required=True)
        parser.add_argument(
            '--junit-xml-path',
            help='JUnit-compatible XML result file of this test',
            required=True)
        parser.add_argument(
            '--language',
            help='The langugage this unit test is written in',
            choices=['cxx', 'java'],
            required=True)
        parser.add_argument(
            '--test-failed',
            help='Flag computed by the Bash-based test framework saying whether the test failed',
            choices=['true', 'false']
        )
        parser.add_argument(
            '--fatal-details-path-prefix',
            help='Prefix of fatal failure details files'
        )
        parser.add_argument(
            '--class-name',
            help='Class name (helpful when there is no test result XML file)'
        )
        parser.add_argument(
            '--test-name',
            help='Test name within the class (helpful when there is no test result XML file)'
        )
        parser.add_argument(
            '--java-module-dir',
            help='Java module directory containing this test'
        )
        parser.add_argument(
            '--cxx-rel-test-binary',
            help='C++ test binary path relative to the build directory')
        parser.add_argument(
            '--extra-error-log-path',
            help='Extra error log path (stdout/stderr of the outermost test invocation)')
        self.args = parser.parse_args()
        self.test_log_path = self.args.test_log_path
        if not os.path.exists(self.test_log_path) and os.path.exists(self.test_log_path + '.gz'):
            self.test_log_path += '.gz'
        logging.info("Log path: %s", self.test_log_path)
        logging.info("JUnit XML path: %s", self.args.junit_xml_path)
        self.real_build_root = os.path.realpath(self.args.build_root)
        self.real_yb_src_root = os.path.realpath(self.args.yb_src_root)
        self.rel_test_log_path = self.path_rel_to_src_root(self.test_log_path)
        self.rel_junit_xml_path = self.path_rel_to_src_root(self.args.junit_xml_path)

        self.rel_extra_error_log_path = None
        if self.args.extra_error_log_path:
            self.rel_extra_error_log_path = self.path_rel_to_src_root(
                self.args.extra_error_log_path)

        self.fatal_details_paths = None
        if self.args.class_name:
            logging.info("Externally specified class name: %s", self.args.class_name)
        if self.args.test_name:
            logging.info("Externally specified test name: %s", self.args.test_name)

        if (not self.args.fatal_details_path_prefix and
                self.args.test_name and
                self.args.test_log_path.endswith('-output.txt')):
            # This mimics the logic in MiniYBCluster.configureAndStartProcess that comes up with a
            # prefix like "org.yb.client.TestYBClient.testKeySpace.fatal_failure_details.", while
            # the Java test log paths are of the form "org.yb.client.TestYBClient-output.txt".
            self.args.fatal_details_path_prefix = '.'.join([
                self.args.test_log_path[:-len('-output.txt')],
                self.args.test_name,
                'fatal_failure_details'
            ]) + '.'

        if self.args.fatal_details_path_prefix:
            self.fatal_details_paths = [
                self.path_rel_to_src_root(path)
                for path in glob.glob(self.args.fatal_details_path_prefix + '*')
            ]

        self.test_descriptor_str = os.environ.get('YB_TEST_DESCRIPTOR')

    def path_rel_to_build_root(self, path):
        return os.path.relpath(os.path.realpath(path), self.real_build_root)

    def path_rel_to_src_root(self, path):
        return os.path.relpath(os.path.realpath(path), self.real_yb_src_root)

    def set_common_test_kvs(self, test_kvs):
        """
        Set common data for all tests produced by this invocation of the script. In practice there
        won't be too much duplication as this script will be invoked for one test at a time.
        """
        test_kvs["language"] = self.args.language
        if self.args.cxx_rel_test_binary:
            test_kvs["cxx_rel_test_binary"] = self.args.cxx_rel_test_binary
        test_kvs["log_path"] = self.rel_test_log_path
        test_kvs["junit_xml_path"] = self.rel_junit_xml_path
        if self.fatal_details_paths:
            test_kvs["fatal_details_paths"] = self.fatal_details_paths
        if self.test_descriptor_str:
            test_kvs["test_descriptor"] = self.test_descriptor_str

        if self.rel_extra_error_log_path:
            test_kvs["extra_error_log_path"] = self.rel_extra_error_log_path

    def run(self):
        junit_xml = ET.parse(self.args.junit_xml_path)
        tests = []
        for test_case_element in junit_xml.iter('testcase'):
            test_kvs = dict(test_case_element.attrib)
            set_element_count_property(test_kvs, test_case_element, 'error', 'num_errors')
            set_element_count_property(test_kvs, test_case_element, 'failure', 'num_failures')

            # In C++ tests, we don't get the "skipped" attribute, but we get the status="notrun"
            # attibute.
            set_element_count_property(
                test_kvs, test_case_element, 'skipped', 'num_skipped',
                increment_by=1 if test_kvs.get('status') == 'notrun' else 0)

            parsing_errors = []
            if "time" in test_kvs and isinstance(test_kvs["time"], str):
                # Remove commas to parse numbers like "1,275.516".
                time_str = test_kvs["time"].replace(",", "")
                try:
                    test_kvs["time"] = float(time_str)
                except ValueError as ex:
                    test_kvs["time"] = None
                    parsing_errors.append(
                        "Could not parse time: %s. Error: %s" % (time_str, str(ex))
                    )
            if parsing_errors:
                test_kvs["parsing_errors"] = parsing_errors
            rename_key(test_kvs, 'name', 'test_name')
            rename_key(test_kvs, 'classname', 'class_name')
            self.set_common_test_kvs(test_kvs)
            tests.append(test_kvs)

        output_path = os.path.splitext(self.args.junit_xml_path)[0] + '_test_report.json'
        if len(tests) == 1:
            tests = tests[0]
        with open(output_path, 'w') as output_file:
            output_file.write(
                json.dumps(tests, indent=2)
            )
        logging.info("Wrote JSON test report file: %s", output_path)


def main():
    postprocessor = Postprocessor()
    postprocessor.run()


if __name__ == '__main__':
    yugabyte_pycommon.init_logging()
    main()
