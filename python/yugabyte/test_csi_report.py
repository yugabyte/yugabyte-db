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
Unit tests for csi_report's behavior when CSI is not configured.

Holding a launch id is not evidence that there is a server to talk to: launch_qid(), create_suite()
and close_item() take the launch as a parameter so the baseline tests can report to their own
launch, so a caller can have one while CSI_SERVER/CSI_TOKEN are unset. Every entry point must then
no-op rather than build 'https:///api/v2/' and raise InvalidURL('No host supplied') - which is how
an unconfigured CSI once aborted a whole test run.
"""

import json
import os
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
    assert csi_report.launch_qid(launch='some-launch-uuid') == ''


def test_create_suite_returns_the_var_name_with_no_value() -> None:
    assert csi_report.create_suite(
        qid='', suite_name='C++', parent='', method='Requested', planned=1, reps=1,
        time_sec=0.0, launch='some-launch-uuid') == ('YB_CSI_C++', '')


def test_close_item_is_a_no_op() -> None:
    assert csi_report.close_item(
        'some-suite-uuid', 0.0, '', [], launch='some-launch-uuid') == ''


def test_create_test_is_a_no_op() -> None:
    descriptor = test_descriptor.TestDescriptor('tests-x/a-test:::A.B')
    assert csi_report.create_test(descriptor, 0.0, 1, rerun=False) == ''


def test_upload_log_is_a_no_op() -> None:
    assert csi_report.upload_log('some-suite-uuid', 0.0, ['/nonexistent/log']) == 0


def test_test_ids_in_launches_is_a_no_op() -> None:
    assert csi_report.test_ids_in_launches('launch_type:baseline_test_run') == set()


def test_previous_launch_unique_ids_is_a_no_op() -> None:
    # None, not an empty set: "no tests are known" would make every test read as new.
    assert csi_report.previous_launch_unique_ids() is None


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
                            planned=planned, reps=1, time_sec=0.0, launch='a-launch-uuid')

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
    instead would report 'nothing run yet' and cost a redundant run on the baseline.
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
# previous_launch_unique_ids: the launch this build's tests are "new" against.
#
# Picking the wrong launch does not produce a slightly wrong answer, it produces a flood: every
# test the picked launch did not report reads as new. So the guards are pinned here.
# ---------------------------------------------------------------------------------------------

class FakeJson(FakeResponse):
    def __init__(self, body: Any) -> None:
        super().__init__(200)
        self.body = body

    def json(self) -> Any:
        return self.body


def attrs(**pairs: str) -> List[Any]:
    return [{'key': key, 'value': value} for key, value in pairs.items()]


def items_page(unique_ids: List[str], total_elements: int, total_pages: int) -> FakeJson:
    return FakeJson({
        'content': [{'uniqueId': unique_id} for unique_id in unique_ids],
        'page': {'totalElements': total_elements, 'totalPages': total_pages},
    })


def suite(name: str, reported: int, **planned: str) -> Any:
    """A suite item as CSI lists it: the planned count as an attribute named for the selection
    method (create_suite), the reported count in its statistics."""
    return {'name': name, 'attributes': attrs(**planned),
            'statistics': {'executions': {'total': reported}}}


# A previous launch whose two suites reached every test they planned.
COMPLETE_SUITES = [suite('C++', reported=12000, All='12000'),
                   suite('Java', reported=900, All='900')]


def serve_csi(monkeypatch: pytest.MonkeyPatch, current: Any, launches: Any,
              item_pages: Any = None, suites: Any = None) -> List[Any]:
    """
    A configured CSI answering the four queries the function makes: this launch, the lane's
    launches, the chosen launch's suites, and one page of its items at a time. Records the params
    of every call.
    """
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'DBFT')
    calls: List[Any] = []

    def get(url: str, headers: Any = None, params: Any = None) -> Any:
        calls.append((url, params))
        if url.endswith('/launch/some-launch-uuid'):
            return FakeJson(current)
        if url.endswith('/launch'):
            return FakeJson({'content': launches})
        if url.endswith('/item') and params['filter.eq.type'] == 'SUITE':
            return FakeJson({'content': COMPLETE_SUITES if suites is None else suites})
        if url.endswith('/item'):
            return (item_pages or [])[int(params['page.page']) - 1]
        raise AssertionError("unexpected CSI query: " + url)

    monkeypatch.setattr(csi_report.requests, 'get', get)
    return calls


def item_queries(calls: List[Any]) -> List[Any]:
    """The params of the step-item (test list) queries among the recorded calls."""
    return [params for url, params in calls
            if url.endswith('/item') and params['filter.eq.type'] == 'step']


def test_previous_launch_is_the_newest_completed_older_one(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Newer launches and the current one are not a baseline, and neither is one that did not run to
    the end: it reported a fraction of its tests and the rest would read as new.
    """
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    launches = [
        {'id': 501, 'status': 'IN_PROGRESS'},   # the next lane run, already started
        {'id': 500, 'status': 'FAILED'},        # this launch
        {'id': 499, 'status': 'INTERRUPTED'},   # aborted: an incomplete test list
        {'id': 498, 'status': 'FAILED'},        # the one to compare against
        {'id': 497, 'status': 'PASSED'},
    ]
    calls = serve_csi(monkeypatch, current, launches, [
        items_page(['a-test:::A.One', 'a-test:::A.Two'], total_elements=3, total_pages=2),
        items_page(['B#three'], total_elements=3, total_pages=2),
    ])

    assert csi_report.previous_launch_unique_ids() == {
        'a-test:::A.One', 'a-test:::A.Two', 'B#three'}

    lane_query = calls[1][1]
    assert lane_query['filter.eq.name'] == 'master-alma8-clang21-tsan'
    # Same lane name, other branch: the family name folds several branches together and only the
    # version tells them apart.
    assert lane_query['filter.has.compositeAttribute'] == 'version:2.31.0.0'
    assert calls[2][1]['filter.eq.type'] == 'SUITE'
    assert calls[2][1]['filter.eq.launchId'] == '498'
    # Two pages of items, no more: the read is a handful of requests per build, not one per test.
    assert [q['filter.eq.launchId'] for q in item_queries(calls)] == ['498', '498']
    assert len(calls) == 5


def test_a_truncated_previous_launch_is_not_a_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A main pass that stopped at its failure threshold still finished its launch, FAILED, with the
    tests it never reached unreported. Every one of those would read as new, so a launch whose
    suites reported fewer tests than they planned is not compared against, and its item list is
    never read.
    """
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    calls = serve_csi(monkeypatch, current, [{'id': 498, 'status': 'FAILED'}], suites=[
        suite('C++', reported=11400, All='12000'),   # stopped 600 tests short
        suite('Java', reported=900, All='900'),
    ])

    assert csi_report.previous_launch_unique_ids() is None
    assert item_queries(calls) == []


def test_a_parent_suite_is_ignored_and_a_null_count_is_not_trusted(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A job may nest the language suites under a parent suite (YB_CSI_SUITE); it plans no tests and
    says nothing about the list. A planned suite whose count attribute is null is not a full pass.
    """
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    parent = {'name': 'DB', 'attributes': [], 'statistics': {'executions': {'total': 12900}}}
    serve_csi(monkeypatch, current, [{'id': 498, 'status': 'PASSED'}],
              [items_page(['a-test:::A.One'], total_elements=1, total_pages=1)],
              suites=[parent] + COMPLETE_SUITES)
    assert csi_report.previous_launch_unique_ids() == {'a-test:::A.One'}

    # Only the parent: nothing was planned that the list could be compared against.
    serve_csi(monkeypatch, current, [{'id': 498, 'status': 'PASSED'}], suites=[parent])
    assert csi_report.previous_launch_unique_ids() is None

    null_count = {'name': 'C++', 'attributes': [{'key': 'All', 'value': None}],
                  'statistics': {'executions': {'total': 12000}}}
    serve_csi(monkeypatch, current, [{'id': 498, 'status': 'PASSED'}], suites=[null_count])
    assert csi_report.previous_launch_unique_ids() is None


def test_a_partial_previous_launch_is_not_a_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """A launch that ran a named or filtered subset planned only that subset; its list says
    nothing about the tests it was never going to run."""
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    calls = serve_csi(monkeypatch, current, [{'id': 498, 'status': 'PASSED'}], suites=[
        suite('C++', reported=40, RegEx='40'),
    ])

    assert csi_report.previous_launch_unique_ids() is None
    assert item_queries(calls) == []


def test_a_per_change_launch_has_no_lane_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """A diff's predecessor is another revision of the same diff, not the lane's previous state."""
    current = {'id': 500, 'name': 'D12345-alma9-clang21-asan',
               'attributes': attrs(diff='3', branch='master')}
    calls = serve_csi(monkeypatch, current, [])

    assert csi_report.previous_launch_unique_ids() is None
    assert len(calls) == 1


def test_no_older_launch_means_no_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """The first launch of a lane: every test is new, and repeating them all is not the point."""
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    serve_csi(monkeypatch, current, [{'id': 500, 'status': 'FAILED'}])

    assert csi_report.previous_launch_unique_ids() is None


def test_an_oversized_item_query_is_abandoned(monkeypatch: pytest.MonkeyPatch) -> None:
    """Far more items than a lane reports is not one lane's list; paging on would be wasted."""
    current = {'id': 500, 'name': 'master-alma8-clang21-tsan',
               'attributes': attrs(version='2.31.0.0', bld='4242')}
    serve_csi(monkeypatch, current, [{'id': 498, 'status': 'PASSED'}], [
        items_page(['a-test:::A.One'],
                   total_elements=csi_report.ITEM_QUERY_LIMIT + 1, total_pages=500),
    ])

    assert csi_report.previous_launch_unique_ids() is None


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


def test_test_ids_in_launches_reads_every_page(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A page size limits the response, not what matches, so both queries have to page to the end.
    Reading only the first page drops test ids silently and the caller runs those tests again - the
    expensive direction, since each repeated test costs baseline_repetitions runs.
    """
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'dbft')

    # Two pages of launches; the launch on each page has two pages of items.
    launch_pages = {
        '1': {'content': [{'id': 11}], 'page': {'totalPages': 2}},
        '2': {'content': [{'id': 22}], 'page': {'totalPages': 2}},
    }
    item_pages = {
        (11, '1'): {'content': [{'uniqueId': 'a'}], 'page': {'totalPages': 2}},
        (11, '2'): {'content': [{'uniqueId': 'b'}], 'page': {'totalPages': 2}},
        (22, '1'): {'content': [{'uniqueId': 'c'}], 'page': {'totalPages': 2}},
        (22, '2'): {'content': [{'uniqueId': 'd'}], 'page': {'totalPages': 2}},
    }

    class FakeResponse:
        def __init__(self, payload: Any) -> None:
            self.status_code = 200
            self._payload = payload

        def json(self) -> Any:
            return self._payload

    def fake_get(url: str, headers: Any = None, params: Any = None) -> Any:
        if url.endswith('/launch'):
            return FakeResponse(launch_pages[params['page.number']])
        return FakeResponse(item_pages[(params['filter.eq.launchId'], params['page.number'])])

    monkeypatch.setattr(csi_report.requests, 'get', fake_get)

    assert csi_report.test_ids_in_launches('launch_type:baseline_test_run') == {'a', 'b', 'c', 'd'}


def test_test_ids_in_launches_asks_only_for_tests_that_reached_a_verdict(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A launch finished while a peer is still reporting leaves that peer's items INTERRUPTED. Reading
    those back as already run would inherit a truncated sample as a complete one, permanently - the
    next diff would skip exactly the tests whose data is missing. The status filter is what keeps
    them eligible, so assert it is on the wire rather than trusting the server to be asked nicely.
    """
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'dbft')

    item_queries: List[Any] = []

    class FakeResponse:
        def __init__(self, payload: Any) -> None:
            self.status_code = 200
            self._payload = payload

        def json(self) -> Any:
            return self._payload

    def fake_get(url: str, headers: Any = None, params: Any = None) -> Any:
        if url.endswith('/launch'):
            return FakeResponse({'content': [{'id': 11}], 'page': {'totalPages': 1}})
        item_queries.append(params)
        return FakeResponse({'content': [{'uniqueId': 'a'}], 'page': {'totalPages': 1}})

    monkeypatch.setattr(csi_report.requests, 'get', fake_get)

    assert csi_report.test_ids_in_launches('launch_type:baseline_test_run') == {'a'}
    assert len(item_queries) == 1
    assert item_queries[0]['filter.in.status'] == 'PASSED,FAILED'


# ---------------------------------------------------------------------------------------------
# truncated_log: what a large log is cut down to. The tail of a failing test's log is teardown
# noise, so the excerpt is centered on the first failure marker when there is one - the earlier
# logic kept the tail unconditionally and dropped the very lines the failure is diagnosed from.
# ---------------------------------------------------------------------------------------------

MARKER = csi_report.LOG_MARKERS[0].decode()


def lines(tag: str, count: int) -> str:
    """Filler log lines, tagged so a test can tell which part of the file an excerpt came from."""
    return "".join(f"{tag} line {i}\n" for i in range(count))


def truncate(tmp_path: Any, content: str, limit: int) -> Any:
    """truncated_log() of a log file holding content."""
    path = tmp_path / "test.log"
    path.write_text(content)
    return csi_report.truncated_log(str(path), path.stat().st_size, limit)


def test_excerpt_is_centered_on_the_marker(tmp_path: Any) -> None:
    text, centered = truncate(
        tmp_path, lines("before", 2000) + MARKER + "\n" + lines("after", 2000), 4000)

    assert centered
    assert MARKER in text
    assert text.startswith(csi_report.CUT_MARK) and text.endswith(csi_report.CUT_MARK)
    # Marker near the middle: roughly as much context before it as after.
    at = text.index(MARKER)
    assert abs(at - (len(text) - at)) < len(text) // 4


def test_first_marker_wins(tmp_path: Any) -> None:
    """A test can fail more than once; the first failure is the root cause of the ones after it."""
    text, centered = truncate(
        tmp_path,
        lines("before", 2000) + f"{MARKER} #0\n" + lines("middle", 2000) + f"{MARKER} #1\n",
        1000)

    assert centered
    assert f"{MARKER} #0" in text
    assert f"{MARKER} #1" not in text


def test_no_marker_keeps_the_tail(tmp_path: Any) -> None:
    text, centered = truncate(tmp_path, lines("plain", 5000), 1000)

    assert not centered
    assert text.startswith(csi_report.CUT_MARK)
    assert not text.endswith(csi_report.CUT_MARK)
    assert "plain line 4999" in text


def test_marker_near_the_start_keeps_the_head(tmp_path: Any) -> None:
    text, _ = truncate(tmp_path, MARKER + "\n" + lines("after", 5000), 1000)

    assert text.startswith(MARKER)


def test_marker_near_the_end_keeps_the_tail(tmp_path: Any) -> None:
    text, _ = truncate(tmp_path, lines("before", 5000) + MARKER + "\n" + lines("after", 2), 1000)

    assert MARKER in text
    assert text.rstrip().endswith("after line 1")


def test_excerpt_respects_the_limit(tmp_path: Any) -> None:
    limit = 1000
    text, _ = truncate(
        tmp_path, lines("before", 2000) + MARKER + "\n" + lines("after", 2000), limit)

    assert len(text) <= limit + 2 * len(csi_report.CUT_MARK)


def test_marker_found_across_a_chunk_boundary(
        tmp_path: Any, monkeypatch: pytest.MonkeyPatch) -> None:
    """The streaming search overlaps its reads, so a marker split between chunks is still seen."""
    monkeypatch.setattr(csi_report, 'MARKER_CHUNK_SIZE', 64)
    filler = "x" * (64 - len(MARKER) // 2)

    text, centered = truncate(tmp_path, f"{filler}\n{MARKER}\n" + lines("after", 100), 100)

    assert centered
    assert MARKER in text


def test_invalid_utf8_does_not_fail(tmp_path: Any) -> None:
    """Daemon logs can carry raw bytes; a cut in mid-character must not abort the upload."""
    path = tmp_path / "test.log"
    path.write_bytes(b"\xff\xfe binary junk\n" * 200 + MARKER.encode() + b"\n" + b"tail\n" * 200)

    text, centered = csi_report.truncated_log(str(path), path.stat().st_size, 500)

    assert centered
    assert MARKER in text


# The new-test repetitions are dispatched serially after the main pass, and only for a test whose
# first attempt passed, so consumers may read the kind on an item as "the first attempt passed".
# That has to hold for every execution of the job, a Spark resubmit inside it included, which is
# why the kind outranks task_resubmit.
@pytest.mark.parametrize('rerun,attempt,attempt_index,expected', [
    (False, 0, 2, (True, 'new_test_repetition', False)),
    (False, 3, 5, (True, 'new_test_repetition', False)),  # resubmit inside the new-test job
    # fail_repetition still wins, so a first-attempt failure can never read as a passing birth.
    (True, 0, 2, (True, 'fail_repetition', False)),
])
def test_classify_execution_new_test(rerun: bool, attempt: int, attempt_index: int,
                                     expected: Any) -> None:
    assert csi_report.classify_execution(
        rerun, attempt, '1', attempt_index, new_test=True) == expected


# ---------------------------------------------------------------------------------------------
# create_test: which executions ask CSI for a previous item before reporting.
#
# The query runs on every worker for every such execution, so an execution that does not need it
# must not make it. A fail repetition and a new-test repetition run serially after the main pass,
# in which their first attempt completed and reported; a repetition or a resubmit cannot know that.
# ---------------------------------------------------------------------------------------------

def serve_create(monkeypatch: pytest.MonkeyPatch) -> List[Any]:
    """A configured CSI that accepts item creation and records each request body, and that
    fails the test if any query is made."""
    monkeypatch.setenv('CSI_SERVER', 'csi.example.com')
    monkeypatch.setenv('CSI_TOKEN', 'a-token')
    monkeypatch.setenv('CSI_PROJ', 'DBFT')
    created: List[Any] = []

    def post(url: str, headers: Any = None, data: Any = None) -> Any:
        created.append(json.loads(data))
        response = FakeJson({'id': 'new-item-uuid'})
        response.status_code = 201
        return response

    def get(url: str, headers: Any = None, params: Any = None) -> Any:
        raise AssertionError("this execution must not query CSI for a previous item: " + url)

    monkeypatch.setattr(csi_report.requests, 'post', post)
    monkeypatch.setattr(csi_report.requests, 'get', get)
    return created


@pytest.mark.parametrize('new_test,rerun,retry_kind', [
    ('1', False, 'new_test_repetition'),
    ('', True, 'fail_repetition'),
])
def test_a_serial_repetition_reports_without_querying(
        monkeypatch: pytest.MonkeyPatch, new_test: str, rerun: bool, retry_kind: str) -> None:
    monkeypatch.setenv('YB_CSI_NEW_TEST', new_test)
    created = serve_create(monkeypatch)
    descriptor = test_descriptor.TestDescriptor('tests-x/a-test:::A.B:::attempt_7')

    assert csi_report.create_test(descriptor, 0.0, 0, rerun=rerun) == 'new-item-uuid'

    [item] = created
    assert item['retry'] is True
    assert item['uniqueId'] == 'tests-x/a-test:::A.B'
    assert {'key': 'retry_kind', 'value': retry_kind} in item['attributes']
