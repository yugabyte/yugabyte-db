#!/usr/bin/env python2.7

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

import logging
import argparse
import os
import sys
import signal
import time

from xml.dom import minidom


MAX_RESULT_XML_SIZE_BYTES = 16 * 1024 * 1024

# This script sometimes gets stuck in our macOS NFS environment for unknown reasons, so we put a
# timeout around it.
TIMEOUT_SEC = 10


# https://stackoverflow.com/questions/2281850/timeout-function-if-it-takes-too-long-to-finish
class Timeout:
    def __init__(self, seconds=1, error_message='Timeout'):
        self.seconds = seconds
        self.start_time = None

    def handle_timeout(self, signum, frame):
        if self.start_time is None:
            raise RuntimeError("Timed out")
        raise RuntimeError("Timed out after %.2f seconds" % (time.time() - self.start_time))

    def __enter__(self):
        self.start_time = time.time()
        signal.signal(signal.SIGALRM, self.handle_timeout)
        signal.alarm(self.seconds)

    def __exit__(self, type, value, traceback):
        signal.alarm(0)


def initialize():
    logging.basicConfig(
        level=logging.INFO,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")


def parse_args():
    parser = argparse.ArgumentParser(
        usage="usage: %(prog)s <options>",
        description="Updates a JUnit-style test result XML file, e.g. to add a log URL or "
                    "to mark the test as failed.")

    parser.add_argument(
        "--result-xml",
        help="The test result XML file (in the JUnit XML format) to update",
        type=unicode,
        dest="result_xml",
        metavar="RESULT_XML_PATH",
        required=True)

    parser.add_argument(
        "--log-url",
        help="The test log URL to insert into the test result XML file. "
             "We need this so we can navigate directly to the test log "
             "from a test result page such as https://jenkins.dev.yugabyte.com/job/"
             "yugabyte-ubuntu-phabricator/131/testReport/junit/(root)/ClientTest"
             "/TestInvalidPredicates/.",
        type=unicode,
        dest="log_url",
        metavar="LOG_URL")

    parser.add_argument(
        "--extra-message",
        help="An extra message to add to the test result.",
        type=unicode,
        metavar="EXTRA_MESSAGE")

    parser.add_argument(
        "--mark-as-failed",
        help="Mark test as failed if it isn't",
        choices=['true', 'false'],
        default='false',
        dest="mark_as_failed")

    return parser.parse_args()


def update_test_result_xml(args):
    result_xml_size = os.stat(args.result_xml).st_size
    if result_xml_size > MAX_RESULT_XML_SIZE_BYTES:
        logging.error(
            "Result XML file size is more than max allowed size (%d bytes): %d bytes. "
            "Refusing to parse this XML file and add the log path there.",
            MAX_RESULT_XML_SIZE_BYTES, result_xml_size)
        return False

    # Basic JUnit XML format description:
    #
    # <testsuites>        => the aggregated result of all junit testfiles
    #   <testsuite>       => the output from a single TestSuite
    #     <properties>    => the defined properties at test execution
    #       <property>    => name/value pair for a single property
    #       ...
    #     </properties>
    #     <error></error> => optional information, in place of a test case -
    #                        normally if the tests in the suite could not be found etc.
    #     <testcase>      => the results from executing a test method
    #       <system-out>  => data written to System.out during the test run
    #       <system-err>  => data written to System.err during the test run
    #       <skipped/>    => test was skipped
    #       <failure>     => test failed
    #       <error>       => test encountered an error
    #     </testcase>
    #     ...
    #   </testsuite>
    #   ...
    # </testsuites>
    #
    # (taken from http://help.catchsoftware.com/display/ET/JUnit+Format).

    xml_dom = minidom.parse(args.result_xml)

    # It might happen that test case passed and produced XML with passed results, but after that
    # fails due to additional instrumented checks (ThreadSanitizer, AddressSanitizer, etc)
    # with non-zero exit code. In this case we need to mark test case as failed by adding <error>
    # node to be recognized as failed by Jenkins. We also updating counter attributes if they
    # present and contain integer values.
    if args.mark_as_failed == 'true':
        for testcase_node in xml_dom.getElementsByTagName('testcase'):
            if (testcase_node.getElementsByTagName('error').length +
                    testcase_node.getElementsByTagName('failure').length == 0):
                error_node = xml_dom.createElement('error')
                testcase_node.appendChild(error_node)
                container_node = testcase_node.parentNode
                # Outermost elements have Document as a parent and Document has no parent.
                while container_node.parentNode:
                    errors_attr = container_node.getAttribute('errors')
                    if errors_attr:
                        try:
                            container_node.setAttribute('errors', str(int(errors_attr) + 1))
                        except ValueError:
                            # just skip
                            pass
                    container_node = container_node.parentNode

    extra_text = ""
    if args.extra_message:
        extra_text += "%s\n\n" % args.extra_message
    if args.log_url:
        extra_text += "Log: %s\n\n" % args.log_url

    if extra_text:
        # We are assuming that there may be some distinction between "failure" and "error" nodes.
        # E.g.  in some xUnit test frameworks "failures" are assertion failures and "errors" are
        # test program crashes happening even before we get to an assertion. We are treating these
        # two XML node types the same for the purpose of adding a log URL here.
        for tag_name in ('failure', 'error'):
            for failure_node in xml_dom.getElementsByTagName(tag_name):
                new_node = xml_dom.createTextNode(extra_text)
                if len(failure_node.childNodes) > 0:
                    failure_node.insertBefore(new_node, failure_node.firstChild)
                else:
                    failure_node.appendChild(new_node)

    output_xml_bytes = xml_dom.toxml().encode('utf-8')
    with open(args.result_xml, 'wb') as output_file:
        output_file.write(output_xml_bytes)

    return True


def main():
    args = None
    try:
        with Timeout(seconds=TIMEOUT_SEC):
            args = parse_args()
            return update_test_result_xml(args)
    except:  # noqa
        if args:
            logging.error("Error while trying to update test result XML: %s", args.result_xml)
        raise


if __name__ == "__main__":
    initialize()
    if main():
        exit_code = 0
    else:
        exit_code = 1
    sys.exit(exit_code)
