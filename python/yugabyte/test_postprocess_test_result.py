# Copyright (c) Yugabyte, Inc.
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

import pathlib
import pytest
import random
from typing import Dict, Any
from yugabyte.postprocess_test_result import Postprocessor, FAIL_TAG_AND_PATTERN, SIGNALS


LOG_CONTENTS = \
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor \n" \
    "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud \n" \
    "exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure \n" \
    "dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. \n" \
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt \n" \
    "mollit anim id est laborum. \n"

ERRORS = [
    'Timeout reached',
    'LeakSanitizer: detected memory leaks',
    'AddressSanitizer: heap-use-after-free',
    'AddressSanitizer: undefined-behavior',
    'UndefinedBehaviorSanitizer: undefined-behavior',
    'ThreadSanitizer: data race',
    'ThreadSanitizer: lock-order-inversion',
    'Leak check XYZ detected leaks',
    'Segmentation fault: ',
    '[  FAILED  ]',
    random.choice(SIGNALS),
    'Check failed: ',
    '[INFO] BUILD FAILURE'
]


@pytest.fixture
def mocked_post_processor() -> Postprocessor:
    return Postprocessor([  # type: ignore
        '--yb-src-root', 'not_a_real_dir',
        '--build-root', ' not_a_real_dir',
        '--test-log-path', 'not_a_real_dir/StringUtilTest_MatchPatternTest.log',
        '--junit-xml-path', ' not_a_real_dir/StringUtilTest_MatchPatternTest.xml',
        '--language', 'cxx'
    ])


@pytest.fixture
def mocked_test_kvs_success() -> Dict[str, Any]:
    return {
        'status': 'run',
        'time': 0.01,
        'test_name': 'MatchPatternTest',
        'class_name': 'StringUtilTest',
        'language': 'cxx',
        'cxx_rel_test_binary': 'tests-gutil/string_util-test',
        'log_path': 'not_a_real_dir/StringUtilTest_MatchPatternTest.log',
        'junit_xml_path': 'not_a_real_dir/StringUtilTest_MatchPatternTest.xml'
    }


@pytest.fixture
def mocked_test_kvs_failure(mocked_test_kvs_success: Dict[str, Any]) -> Dict[str, Any]:
    return {**mocked_test_kvs_success, **{'num_errors': 1, 'num_failures': 1}}


def test_add_fail_tags_success(mocked_post_processor: Postprocessor,
                               mocked_test_kvs_success: Dict[str, Any]) -> None:
    test_kvs = mocked_test_kvs_success
    mocked_post_processor.set_fail_tags(test_kvs)
    assert 'fail_tags' not in test_kvs


def test_processing_errors(mocked_post_processor: Postprocessor,
                           mocked_test_kvs_failure: Dict[str, Any]) -> None:
    test_kvs = mocked_test_kvs_failure
    mocked_post_processor.test_log_path = '/not/a/real/file.log'
    mocked_post_processor.set_fail_tags(test_kvs)
    assert 'processing_errors' in test_kvs
    assert 'fail_tags' not in test_kvs


@pytest.mark.parametrize("error_text, expected_tag",
                         list(zip(ERRORS, FAIL_TAG_AND_PATTERN.keys())))
def test_add_fail_tag_failures(mocked_post_processor: Postprocessor,
                               mocked_test_kvs_failure: Dict[str, Any],
                               error_text: str, expected_tag: str,
                               tmp_path: pathlib.Path) -> None:
    test_kvs = mocked_test_kvs_failure
    # Write to temp file using built-in pytest fixture under /tmp/pytest-of-$USER/pytest-current
    # Temp directories older than 3 most recent runs are automatically removed.
    test_log = tmp_path / 'failed_test.log'
    test_log.write_text(LOG_CONTENTS + error_text, encoding='utf-8')
    mocked_post_processor.test_log_path = str(test_log)
    mocked_post_processor.set_fail_tags(test_kvs)
    assert 'fail_tags' in test_kvs
    assert len(test_kvs['fail_tags']) == 1
    if expected_tag.startswith('signal_'):
        assert test_kvs['fail_tags'] == [expected_tag.format(error_text)]
    else:
        assert test_kvs['fail_tags'] == [expected_tag]


def test_add_fail_tag_multiline(mocked_post_processor: Postprocessor,
                                mocked_test_kvs_failure: Dict[str, Any],
                                tmp_path: pathlib.Path) -> None:
    test_kvs = mocked_test_kvs_failure
    test_log = tmp_path / 'failed_test.log'
    test_log.write_text(LOG_CONTENTS + 'SIGINT\n'*2 + 'SIGKILL\n'*3 + 'SIGSEGV\n'*4,
                        encoding='utf-8')
    mocked_post_processor.test_log_path = str(test_log)
    mocked_post_processor.set_fail_tags(test_kvs)
    assert 'fail_tags' in test_kvs
    assert len(test_kvs['fail_tags']) == 3
    assert test_kvs['fail_tags'] == ['signal_SIGINT', 'signal_SIGKILL', 'signal_SIGSEGV']
