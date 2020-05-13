#!/usr/bin/env python

"""
Aggregates test reports from JSON-based test report files produced by postprocess_test_result.py.
Takes the set of ..._test_report.json files on standard input -- that is best generated with a
find command. Produces a test_results.json file with the following format:

{
  "total_errors": 0,
  "total_skipped": 296,
  "total_failures": 1,
  "total_test_instances": 4086,
  "total_tests_run": 4062,
  "build_root": "build/asan-clang-dynamic-enterprise-ninja",
  "aggregation_errors": [],
  "compiler_type": "clang",
  "build_type": "asan",
  "tests": [
    {
      "language": "java",
      "class_name": "org.yb.cql.TestSelect",
      "junit_xml_path": "java/yb-cql/target/...",
      "time": 12.804,
      "log_path": "java/yb-cql/target/...",
      "test_name": "testQualifiedColumnReference"
    },
    ...
  ]
}

Also produces a shorter test_failures.json with the following format:

{
  "total_errors": 0,
  "total_skipped": 296,
  "total_failures": 1,
  "total_test_instances": 4086,
  "total_tests_run": 4062,
  "build_root": "build/asan-clang-dynamic-enterprise-ninja",
  "aggregation_errors": [],
  "compiler_type": "clang",
  "build_type": "asan",
  "successful_tests": [
    [
      "AdminCliTest",
      "BlackList"
    ],
    ...
  ],
  "skipped_tests": [
    [
      "AdminCliTest",
      "DISABLED_TestTLS"
    ],
    ...
  ],
  "failures": [
    {
      "status": "run",
      "language": "cxx",
      "class_name": "TestRedisService",
      "junit_xml_path": "build/asan-clang-dynamic-enterprise-ninja/...",
      "test_name": "TestTimeSeriesTTL",
      "extra_error_log_path": "build/asan-clang-dynamic-enterprise-ninja/...",
      "log_path": "build/asan-clang-dynamic-enterprise-ninja/yb-test-logs/...",
      "cxx_rel_test_binary": "tests-redisserver/redisserver-test",
      "num_failures": 1,
      "time": 59.223
    },
    ...
  ]
}
"""

import argparse
import json
import logging
import os
import sys
import yugabyte_pycommon

# for python2/3 compatibility
try:
    from json.decoder import JSONDecodeError
except ImportError:
    JSONDecodeError = ValueError


def is_test_failure(report):
    for key in ['num_errors', 'num_failures']:
        value = report.get(key, 0)
        if value > 0:
            return True
    return False


def is_test_skipped(report):
    return report.get('num_skipped', 0) > 0 or report.get('status') == 'notrun'


def get_zero_one_counter(report, key):
    if report.get(key, 0) == 0:
        return 0
    return 1


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__)
    parser.add_argument(
        '--yb-src-root',
        help='YugaByte source directory. Needed for making paths relative.',
        required=True)
    parser.add_argument(
        '--output-dir',
        help='Output directory to generate aggregated output files in.')
    parser.add_argument(
        '--build-type',
        help='YugaByte build type. Added to test result files.')
    parser.add_argument(
        '--compiler-type',
        help='C/C++ compiler type. Added to test result files.')
    parser.add_argument(
        '--build-root',
        help='Root directory for build artifacts (not including Java). Added to test result files.',
        required=True)

    return parser.parse_args()


def get_test_set(all_test_reports, predicate):
    return sorted(set([
        (report["class_name"], report["test_name"])
        for report in all_test_reports if predicate(report)
    ]))


def add_counters(dest, src):
    for k in src:
        dest[k] = dest.get(k, 0) + src[k]


def aggregate_test_reports(args):
    all_test_reports = []
    failure_reports = []
    errors = []

    for file_path in sys.stdin:
        file_path = os.path.realpath(file_path.strip())
        try:
            with open(file_path) as input_file:
                report = json.load(input_file)
        except IOError, ex:
            errors.append("Failed reading file %s: %s" % (file_path, ex))
        # Catch other cases such as a readable, but empty file.
        except JSONDecodeError:
            errors.append("Failed to parse file %s" % (file_path))

        if isinstance(report, list):
            all_test_reports.extend(report)
            logging.info("JSON test report file has multiple results: %s", file_path)
        else:
            all_test_reports.append(report)

    logging.info("Collected %d test report files", len(all_test_reports))

    totals = {}
    by_language = {}
    for test_report in all_test_reports:
        tests_run_delta = 0
        tests_skipped_delta = 0
        if is_test_skipped(test_report):
            tests_skipped_delta = 1
        else:
            tests_run_delta = 1
        deltas = {
            "errors": get_zero_one_counter(test_report, "num_errors"),
            "failures": get_zero_one_counter(test_report, "num_failures"),
            "skipped": tests_skipped_delta,
            "run": tests_run_delta,
            "test_instances": 1
        }
        if is_test_failure(test_report):
            failure_reports.append(test_report)
        language = test_report["language"]
        if language not in by_language:
            by_language[language] = {}
        add_counters(totals, deltas)
        add_counters(by_language[language], deltas)

    top_level_details = {
        "totals": totals,
        "by_language": by_language,
        "aggregation_errors": errors,
        "build_type": args.build_type,
        "compiler_type": args.compiler_type,
        "build_root": os.path.relpath(
            os.path.realpath(args.build_root),
            os.path.realpath(args.yb_src_root)
        )
    }

    failure_only_report = dict(top_level_details)
    failure_only_report["failures"] = failure_reports
    failure_only_report["successful_tests"] = get_test_set(
        all_test_reports,
        lambda test_report: not is_test_failure(test_report) and not is_test_skipped(test_report))
    failure_only_report["skipped_tests"] = get_test_set(
        all_test_reports, lambda test_report: is_test_skipped(test_report))

    full_report = dict(top_level_details)
    full_report["tests"] = all_test_reports

    full_report_output_path = os.path.join(args.output_dir, 'test_results.json')
    logging.info("Writing full test report to %s", full_report_output_path)
    with open(full_report_output_path, 'w') as output_file:
        json.dump(full_report, output_file, indent=2)

    failure_only_output_path = os.path.join(args.output_dir, 'test_failures.json')
    logging.info("Writing failure-only test report to %s", failure_only_output_path)
    with open(failure_only_output_path, 'w') as output_file:
        json.dump(failure_only_report, output_file, indent=2)

    logging.info("Stats:\n%s", json.dumps(top_level_details, indent=2))


if __name__ == '__main__':
    yugabyte_pycommon.init_logging()
    args = parse_args()
    aggregate_test_reports(args),
