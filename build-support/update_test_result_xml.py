#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

import logging
import argparse
import os
import sys

from xml.dom import minidom


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

    args = parser.parse_args()

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


if __name__ == "__main__":
    sys.exit(main())
