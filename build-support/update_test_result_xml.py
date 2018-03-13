#!/usr/bin/env python

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

from xml.dom import minidom


MAX_RESULT_XML_SIZE_BYTES = 16 * 1024 * 1024


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    parser = argparse.ArgumentParser(
        usage="usage: %(prog)s <options>",
        description="Updates a JUnit-formatted test result XML to include the given URL pointing " +
                    "to the test log. We need this so we can navigate directly to the test log " +
                    "from a test result page such as https://jenkins.dev.yugabyte.com/job/" +
                    "yugabyte-ubuntu-phabricator/131/testReport/junit/(root)/ClientTest" +
                    "/TestInvalidPredicates/.")

    parser.add_argument(
        "--result-xml",
        help="The test result XML file (in the JUnit XML format) to update",
        type=unicode,
        dest="result_xml",
        metavar="RESULT_XML_PATH",
        required=True)

    parser.add_argument(
        "--log-url",
        help="The test log URL to insert into the test result XML file",
        type=unicode,
        dest="log_url",
        metavar="LOG_URL",
        required=True)

    parser.add_argument(
        "--mark-as-failed",
        help="Mark test as failed if it isn't",
        choices=['true', 'false'],
        default='false',
        dest="mark_as_failed")

    args = parser.parse_args()
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

    log_url_with_prefix = "Log: %s\n\n" % args.log_url

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

    # I am assuming that there may be some distinction between "failure" and "error" nodes. E.g. in
    # some xUnit test frameworks "failures" are assertion failures and "errors" are test program
    # crashes happening even before we get to an assertion. We are treating these two XML node types
    # the same for the purpose of adding a log URL here.
    for tag_name in ('failure', 'error'):
        for failure_node in xml_dom.getElementsByTagName(tag_name):
            new_node = xml_dom.createTextNode(log_url_with_prefix)
            if len(failure_node.childNodes) > 0:
                failure_node.insertBefore(new_node, failure_node.firstChild)
            else:
                failure_node.appendChild(new_node)

    output_xml_str = xml_dom.toxml()
    with open(args.result_xml, 'w') as output_file:
        output_file.write(output_xml_str)

    return True


if __name__ == "__main__":
    if main():
        exit_code = 0
    else:
        exit_code = 1
    sys.exit(exit_code)
