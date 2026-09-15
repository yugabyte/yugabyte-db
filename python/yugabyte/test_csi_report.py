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
Unit tests for csi_report: the no-op behavior when CSI is not configured, and the query retries.

Holding a launch id is not evidence that there is a server to talk to: YB_CSI_LID can be set while
CSI_SERVER/CSI_TOKEN are not. Every entry point must then no-op rather than build
'https:///api/v2/' and raise InvalidURL('No host supplied') - which is how an unconfigured CSI once
aborted a whole test run.
"""

import json
from typing import Any, Dict, List

import pytest

# Import the module, not TestDescriptor, so pytest does not collect the Test*-named class.
from yugabyte import csi_report
from yugabyte import test_descriptor


@pytest.fixture(autouse=True)
def no_csi(monkeypatch: pytest.MonkeyPatch) -> None:
    """CSI unset but a launch present - the combination the guards exist for."""
    monkeypatch.delenv('CSI_SERVER', raising=False)
    monkeypatch.delenv('CSI_TOKEN', raising=False)
    monkeypatch.delenv('CSI_PROJ', raising=False)
    monkeypatch.setenv('YB_CSI_LID', 'some-launch-uuid')
    monkeypatch.setenv('YB_CSI_C++', 'some-suite-uuid')

    def no_requests(*args: Any, **kwargs: Any) -> Any:
        raise AssertionError("CSI is not configured; no HTTP request should have been attempted")

    for method in ('get', 'post', 'put'):
        monkeypatch.setattr(csi_report.requests, method, no_requests)


def test_configured_needs_both_server_and_token(monkeypatch: pytest.MonkeyPatch) -> None:
    assert not csi_report.configured()
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    assert not csi_report.configured()          # token still missing
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    assert csi_report.configured()


def test_launch_qid_is_a_no_op() -> None:
    assert csi_report.launch_qid() == ''


def test_create_suite_returns_the_var_name_with_no_value() -> None:
    assert csi_report.create_suite(
        qid='', suite_name='C++', parent='', method='Requested', planned=1, reps=1,
        time_sec=0.0) == ('YB_CSI_C++', '')


def test_close_item_is_a_no_op() -> None:
    assert csi_report.close_item('some-suite-uuid', 0.0, '', []) == ''


def test_create_test_is_a_no_op() -> None:
    descriptor = test_descriptor.TestDescriptor('tests-x/a-test:::A.B')
    assert csi_report.create_test(descriptor, 0.0, 1, rerun=False) == ''


def test_upload_log_is_a_no_op() -> None:
    assert csi_report.upload_log('some-suite-uuid', 0.0, ['/nonexistent/log']) == 0


# ---------------------------------------------------------------------------------------------
# create_suite: the update branch must replace a same-key count attribute (All/RegEx/Requested),
# not append beside it. RP's /item/update replaces the whole attribute set but dedupes only on
# (key, value), not key, so appending left two entries with the same key and different values
# whenever a rerun changed the planned count (DEVOPS-3595).
#
# These bypass the no_csi fixture's requests trap the same way the get_with_retries tests below
# do - CSI must look configured for create_suite to get past its guards.
# ---------------------------------------------------------------------------------------------

class FakeJsonResponse:
    def __init__(self, status_code: int, payload: Any = None) -> None:
        self.status_code = status_code
        self.text = f"body for {status_code}"
        self._payload = payload

    def json(self) -> Any:
        return self._payload


@pytest.fixture
def csi_configured(monkeypatch: pytest.MonkeyPatch) -> None:
    """Undo no_csi's env wipe - these tests need CSI to look configured."""
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'proj')


def existing_suite_response(attributes: List[Dict[str, Any]]) -> FakeJsonResponse:
    """The GET /item response create_suite sees for a suite that already exists."""
    return FakeJsonResponse(200, {'content': [
        {'id': 42, 'uuid': 'suite-item-uuid', 'attributes': attributes}]})


@pytest.mark.parametrize('existing,method,planned,expected_attributes', [
    # Same-method rerun with a changed count: the old value must not survive alongside the new
    # one - this is the bug itself.
    ([{'key': 'All', 'value': '500'}], 'All', 497,
     [{'key': 'All', 'value': 497}]),
    ([{'key': 'Requested', 'value': '12'}], 'Requested', 5,
     [{'key': 'Requested', 'value': 5}]),
    # A method switch (full run -> targeted rerun) must leave the other count key alone: 'All'
    # is the suite-wide planned-total baseline that rerun_list()/check_tests_all_ran compare
    # execution totals against, so it must survive a run that used a different method.
    ([{'key': 'All', 'value': '500'}], 'Requested', 12,
     [{'key': 'All', 'value': '500'}, {'key': 'Requested', 'value': 12}]),
    # Attributes for anything other than the count key being written must survive untouched.
    ([{'key': 'All', 'value': '500'}, {'key': 'tag', 'value': 'nightly'}], 'All', 497,
     [{'key': 'tag', 'value': 'nightly'}, {'key': 'All', 'value': 497}]),
])
def test_update_replaces_only_the_current_method_key(
        monkeypatch: pytest.MonkeyPatch, csi_configured: None,
        existing: List[Dict[str, Any]], method: str, planned: int,
        expected_attributes: List[Dict[str, Any]]) -> None:
    monkeypatch.setattr(
        csi_report.requests, 'get', lambda *a, **k: existing_suite_response(existing))
    put_bodies: List[Dict[str, Any]] = []

    def fake_put(url: str, headers: Any = None, data: Any = None) -> Any:
        put_bodies.append(json.loads(data))
        return FakeJsonResponse(200)

    monkeypatch.setattr(csi_report.requests, 'put', fake_put)

    csi_report.create_suite(qid='1', suite_name='C++', parent='', method=method,
                            planned=planned, reps=1, time_sec=0.0)

    assert len(put_bodies) == 1
    assert put_bodies[0]['attributes'] == expected_attributes


# ---------------------------------------------------------------------------------------------
# get_with_retries: a transient CSI failure must not decide a query's answer.
#
# These bypass the no_csi fixture's requests.get trap by replacing it themselves - the retry
# helper is below the configured() guards and is tested directly.
# ---------------------------------------------------------------------------------------------

class FakeResponse:
    def __init__(self, status_code: int) -> None:
        self.status_code = status_code
        self.text = f"body for {status_code}"


@pytest.fixture
def no_sleep(monkeypatch: pytest.MonkeyPatch) -> None:
    """The retry delay is real seconds; the test should not pay them."""
    monkeypatch.setattr(csi_report.time, 'sleep', lambda seconds: None)


def fake_get(monkeypatch: pytest.MonkeyPatch, outcomes: List[Any]) -> List[int]:
    """
    Serve one outcome per call - a FakeResponse to return or an exception to raise - and record
    how many calls were made.
    """
    calls = []

    def get(url: str, headers: Any = None, params: Any = None) -> Any:
        calls.append(1)
        outcome = outcomes[len(calls) - 1]
        if isinstance(outcome, Exception):
            raise outcome
        return outcome

    monkeypatch.setattr(csi_report.requests, 'get', get)
    return calls


def test_first_attempt_succeeds_without_retrying(
        monkeypatch: pytest.MonkeyPatch, no_sleep: None) -> None:
    ok = FakeResponse(200)
    calls = fake_get(monkeypatch, [ok])

    assert csi_report.get_with_retries('http://csi/x', {}, {}) is ok
    assert len(calls) == 1


@pytest.mark.parametrize('transient', [
    FakeResponse(504),                              # the gateway timeouts CSI actually returns
    csi_report.requests.RequestException('connection reset'),
])
def test_transient_failure_is_retried_then_succeeds(
        monkeypatch: pytest.MonkeyPatch, no_sleep: None, transient: Any) -> None:
    """
    A 5xx and a dropped connection are the same thing to a query: try again. Returning None here
    instead would report 'nothing measured yet' and cost a redundant re-measurement.
    """
    ok = FakeResponse(200)
    calls = fake_get(monkeypatch, [transient, ok])

    assert csi_report.get_with_retries('http://csi/x', {}, {}) is ok
    assert len(calls) == 2


def test_returns_none_once_the_attempts_are_spent(
        monkeypatch: pytest.MonkeyPatch, no_sleep: None) -> None:
    """Callers must get None - not an exception - so a CSI outage degrades instead of failing."""
    calls = fake_get(monkeypatch, [FakeResponse(500)] * csi_report.GET_ATTEMPTS)

    assert csi_report.get_with_retries('http://csi/x', {}, {}) is None
    assert len(calls) == csi_report.GET_ATTEMPTS


def test_delay_grows_with_the_attempt(monkeypatch: pytest.MonkeyPatch) -> None:
    """A busy server gets a longer pause each time round, and none after the last attempt."""
    slept: List[float] = []
    monkeypatch.setattr(csi_report.time, 'sleep', lambda seconds: slept.append(seconds))
    fake_get(monkeypatch, [FakeResponse(503)] * csi_report.GET_ATTEMPTS)

    assert csi_report.get_with_retries('http://csi/x', {}, {}) is None
    assert slept == [csi_report.GET_RETRY_DELAY_SEC * attempt
                     for attempt in range(1, csi_report.GET_ATTEMPTS)]


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
