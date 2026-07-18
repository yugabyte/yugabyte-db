#!/usr/bin/env python3

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


import os
import logging

import xml

from xml.dom import minidom


MAX_RESULT_XML_SIZE_BYTES = 16 * 1024 * 1024


def parse_junit_test_result_xml(file_path: str) -> xml.dom.minidom.Document:
    result_xml_size = os.stat(file_path).st_size
    if result_xml_size > MAX_RESULT_XML_SIZE_BYTES:
        error_str = (
            "Result XML file size is more than max allowed size (%d bytes): %d bytes. "
            "Refusing to parse this XML file and add the log path there." %
            (MAX_RESULT_XML_SIZE_BYTES, result_xml_size))
        logging.error(error_str)
        raise IOError(error_str)

    return minidom.parse(file_path)
