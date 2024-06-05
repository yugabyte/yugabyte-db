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

import os

from yugabyte import analyze_test_results
from yugabyte.common_util import init_logging
from yugabyte.test_util import assert_data_classes_equal


def test_analyze_test_results() -> None:
    init_logging(verbose=True)
    test_data_base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_data')
    aggregated_test_report_path = os.path.join(
        test_data_base_dir, 'clang16_debug_centos7_test_report_from_junit_xml.json')
    run_tests_on_spark_full_report = os.path.join(
        test_data_base_dir,
        'clang16_debug_centos7_run_tests_on_spark_full_report.json')
    analyzer = analyze_test_results.SingleBuildAnalyzer(
        aggregated_test_report_path,
        run_tests_on_spark_full_report,
        archive_dir=None)
    result = analyzer.analyze()
    assert_data_classes_equal(
        result,
        analyze_test_results.AnalysisResult(
            num_tests_in_junit_xml=384,
            num_tests_in_spark=385,
            num_failed_tests_in_junit_xml=2,
            num_failed_tests_in_spark=2,
            num_unique_failed_tests=3,
            num_dedup_errors_in_junit_xml=0,
            num_total_unique_tests=387,
            num_tests_failed_in_spark_but_not_junit_xml=1,
            num_tests_failed_in_junit_xml_but_not_spark=1,
            num_tests_without_junit_xml_report=3,
            num_tests_without_spark_report=1,
            num_successful_tests=379,
            num_unique_tests_present_in_both_report_types=382
        ))


if __name__ == '__main__':
    test_analyze_test_results()
