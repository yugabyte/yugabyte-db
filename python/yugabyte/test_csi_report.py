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


"""
Unit tests for csi_report.classify_execution, the retry_kind decision table, and for the
conn_mgr attribute on created test items.

Backport note: master's test_csi_report.py also covers the CSI GET retry helpers, which
do not exist on this branch; only the classify_execution table and the conn_mgr tests are
carried here.
"""

import json
from typing import Any, Dict, List

import pytest

from yugabyte import csi_report
from yugabyte import test_descriptor


# ---------------------------------------------------------------------------------------------
# classify_execution: the retry_kind decision table. Only fail_repetition vs repetition is
# exclusive by construction (--fail_repetitions is rejected alongside --num_repetitions > 1);
# a Spark task resubmit (attempt > 0) can occur inside either job, and the branch order in
# classify_execution resolves those overlaps: fail_repetition wins (the "first attempt failed"
# implication must stay exact for consumers), then task_resubmit (needs the resubmit wait),
# then repetition. The kind attribute is what lets downstream consumers separate a
# fail-repetition (which must never enter a first-attempt failure rate) from a Spark task
# resubmit (infra artifact) and a plain repetition.

@pytest.mark.parametrize('rerun,attempt,reps,attempt_index,expected', [
    # expected = (retry, retry_kind, wait)
    (False, 0, '1', 1, (False, '', False)),                # plain first attempt
    (True, 0, '1', 1, (True, 'fail_repetition', False)),   # --fail_repetitions re-run
    (True, 0, '1', 3, (True, 'fail_repetition', False)),
    (False, 1, '1', 1, (True, 'task_resubmit', True)),     # Spark re-ran a dead task
    (False, 2, '1', 1, (True, 'task_resubmit', True)),
    (False, 0, '10', 1, (True, 'repetition', False)),      # first repetition may skip the wait
    (False, 0, '10', 3, (True, 'repetition', True)),
    # Overlaps, resolved by precedence:
    (True, 1, '1', 2, (True, 'fail_repetition', False)),   # resubmit inside the rerun job
    (False, 1, '10', 3, (True, 'task_resubmit', True)),    # resubmit inside a repetitions job
])
def test_classify_execution(rerun: bool, attempt: int, reps: str, attempt_index: int,
                            expected: Any) -> None:
    assert csi_report.classify_execution(rerun, attempt, reps, attempt_index) == expected


class FakeCreated:
    status_code = 201

    def json(self) -> Any:
        return {'id': 'new-item-uuid'}


def serve_create(monkeypatch: pytest.MonkeyPatch) -> List[Any]:
    """A configured CSI that accepts item creation and records each request body, and that
    fails the test if any query is made."""
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'DBFT')
    monkeypatch.setenv('YB_CSI_LID', 'some-launch-uuid')
    monkeypatch.delenv('YB_CSI_REPS', raising=False)
    created: List[Any] = []

    def post(url: str, headers: Any = None, data: Any = None) -> Any:
        created.append(json.loads(data))
        return FakeCreated()

    def get(url: str, headers: Any = None, params: Any = None) -> Any:
        raise AssertionError("this execution must not query CSI for a previous item: " + url)

    monkeypatch.setattr(csi_report.requests, 'post', post)
    monkeypatch.setattr(csi_report.requests, 'get', get)
    return created


# The conn_mgr attribute must follow the env vars each language's test harness reads: detective
# builds the rerun command from it.
@pytest.mark.parametrize('descriptor,env,tagged', [
    ('org.yb.pgsql.TestFoo#testBar', {}, False),
    ('org.yb.pgsql.TestFoo#testBar', {'YB_ENABLE_YSQL_CONN_MGR_IN_TESTS': 'true'}, True),
    ('org.yb.pgsql.TestFoo#testBar', {'YB_ENABLE_YSQL_CONN_MGR_IN_JAVA_TESTS': 'TRUE'}, True),
    ('org.yb.pgsql.TestFoo#testBar', {'YB_ENABLE_YSQL_CONN_MGR_IN_TESTS': 'false'}, False),
    ('tests-x/a-test:::A.B', {'YB_ENABLE_YSQL_CONN_MGR_IN_TESTS': 'true'}, True),
    # The Java-only override does not reach C++ tests.
    ('tests-x/a-test:::A.B', {'YB_ENABLE_YSQL_CONN_MGR_IN_JAVA_TESTS': 'true'}, False),
])
def test_create_test_tags_conn_mgr(
        monkeypatch: pytest.MonkeyPatch, descriptor: str, env: Dict[str, str],
        tagged: bool) -> None:
    monkeypatch.delenv('YB_ENABLE_YSQL_CONN_MGR_IN_TESTS', raising=False)
    monkeypatch.delenv('YB_ENABLE_YSQL_CONN_MGR_IN_JAVA_TESTS', raising=False)
    for name, value in env.items():
        monkeypatch.setenv(name, value)
    monkeypatch.setenv('YB_CSI_Java', 'some-suite-uuid')
    monkeypatch.setenv('YB_CSI_C++', 'some-suite-uuid')
    created = serve_create(monkeypatch)

    assert csi_report.create_test(
        test_descriptor.TestDescriptor(descriptor), 0.0, 0, rerun=False) == 'new-item-uuid'

    [item] = created
    assert ({'key': 'conn_mgr', 'value': 'true'} in item['attributes']) == tagged
