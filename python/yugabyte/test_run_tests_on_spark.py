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
Unit tests for the failed-test re-run orchestration in run_tests_on_spark.py, specifically
run_tests_job_with_resubmits(): it re-submits the Spark job for test attempts that did not produce
a result (e.g. because the Spark application was lost while autoscaled workers were shutting down),
re-creating the Spark context when it was stopped.

These tests mock out Spark: run_tests_job(), spark_context_is_stopped() and restart_spark_context()
are patched, so no Spark cluster (or pyspark) is exercised. Only the driver-side orchestration
logic is under test.
"""

import types
from typing import List, Set

import pytest

# Import the module (not the TestDescriptor class) so pytest does not try to collect the
# Test*-named class as a test case.
from yugabyte import run_tests_on_spark as rts
from yugabyte import test_descriptor
from yugabyte import yb_dist_tests


def make_attempts(base: str, num_repetitions: int) -> List[test_descriptor.TestDescriptor]:
    """Expand one failed test into its per-attempt descriptors, the same way main() does."""
    base_descriptor = test_descriptor.TestDescriptor(base)
    return [base_descriptor.with_attempt_index(i) for i in range(1, num_repetitions + 1)]


def make_result(descriptor_str: str, exit_code: int = 0) -> yb_dist_tests.TestResult:
    """A minimal TestResult carrying only the fields the re-run orchestration reads."""
    return yb_dist_tests.TestResult(
        test_descriptor=test_descriptor.TestDescriptor(descriptor_str),
        exit_code=exit_code,
        elapsed_time_sec=0.0,
        failed_without_output=False,
        artifact_paths=None,
        artifact_copy_result=None,
        spark_error_copy_result=None)


def results_for(
        descriptors: List[test_descriptor.TestDescriptor]) -> List[yb_dist_tests.TestResult]:
    return [make_result(td.descriptor_str) for td in descriptors]


def descriptor_strs(descriptors: List[test_descriptor.TestDescriptor]) -> Set[str]:
    return set(td.descriptor_str for td in descriptors)


@pytest.fixture(autouse=True)
def isolate_spark(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Neutralize the real Spark-touching helpers and the resubmit delay, and reset the
    module-global cancellation flag before each test.
    """
    monkeypatch.setattr(rts.time, "sleep", lambda *_args, **_kwargs: None)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setattr(rts, "restart_spark_context", lambda: None)
    monkeypatch.setattr(rts, "g_spark_job_cancelled", False)
    # Ensure the test-only fault hooks are off, so the behavioral tests are not perturbed by a
    # developer running with these set in their environment.
    monkeypatch.delenv("YB_TEST_RERUN_DROP_RESULTS", raising=False)
    monkeypatch.delenv("YB_TEST_RERUN_STOP_CONTEXT", raising=False)


def test_all_attempts_complete_on_first_submission(monkeypatch: pytest.MonkeyPatch) -> None:
    """Happy path: every attempt returns a result, so there is exactly one submission."""
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 1
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_worker_dies_after_5_iterations_only_missing_resubmitted(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A worker/app dies after 5 of the 10 iterations of a test have results. The 5 completed
    iterations must be kept and only the 5 missing ones re-submitted -- not all 10.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[str]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # First submission loses the second half; the resubmission returns everything it is given.
        returned = list(pending)[:5] if len(submissions) == 1 else list(pending)
        return results_for(returned)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 2
    # Only the 5 attempts missing a result are resubmitted.
    assert len(submissions[1]) == 5
    # No completed attempt is resubmitted.
    assert set(submissions[1]).isdisjoint(set(submissions[0][:5]))
    # All 10 iterations end up with a result.
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_context_recreated_only_when_stopped(monkeypatch: pytest.MonkeyPatch) -> None:
    """restart_spark_context() is called before a resubmission iff the context is stopped."""
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    submissions: List[List[test_descriptor.TestDescriptor]] = []
    restart_count = 0

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        # Never return a result for the last attempt, forcing repeated resubmissions.
        return results_for([td for td in pending if td.attempt_index != 4])

    def record_restart() -> None:
        nonlocal restart_count
        restart_count += 1

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "SPARK_JOB_MAX_SUBMITS", 3)

    # Context reports stopped -> restart before each resubmission (submissions 2 and 3).
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    rts.run_tests_job_with_resubmits(attempts, rerun=True)
    assert len(submissions) == 3
    assert restart_count == 2

    # Context reports alive -> never restart, even though resubmissions still happen.
    submissions.clear()
    restart_count = 0
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)
    rts.run_tests_job_with_resubmits(attempts, rerun=True)
    assert len(submissions) == 3
    assert restart_count == 0


def test_cancellation_stops_resubmission(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    When the Spark job was cancelled after hitting the test-failure threshold, the wrapper must
    not fight that decision by resubmitting the (by-design) missing attempts.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        # Simulate run_spark_action detecting the deliberate cancellation.
        rts.g_spark_job_cancelled = True
        return results_for(list(pending)[:3])

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 1
    assert len(results) == 3


def test_permanent_loss_exhausts_submission_budget(monkeypatch: pytest.MonkeyPatch) -> None:
    """If no results ever come back, the wrapper tries at most SPARK_JOB_MAX_SUBMITS times."""
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return []

    monkeypatch.setattr(rts, "SPARK_JOB_MAX_SUBMITS", 5)
    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 5
    assert results == []
    # Every submission retries the full set, since nothing ever completes.
    assert all(len(sub) == len(attempts) for sub in submissions)


def test_java_and_cpp_attempts_matched_independently(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    descriptor_str matching must work across languages and across the attempt-1 (bare) vs
    attempt-N (suffixed) descriptor forms, so completed/pending bookkeeping is exact.
    """
    attempts = (make_attempts("tests-x/a-test:::A.B", 3) +
                make_attempts("com.yb.TestBar#testBaz[1]", 3))
    submissions: List[List[str]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # Lose exactly one attempt of each test on the first pass.
        if len(submissions) == 1:
            return results_for([td for td in pending if td.attempt_index != 2])
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 2
    # The two attempt_2 descriptors (one Java, one C++) are exactly what gets resubmitted.
    assert set(submissions[1]) == {
        "tests-x/a-test:::A.B:::attempt_2",
        "com.yb.TestBar#testBaz[1]:::attempt_2",
    }
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_fault_hook_is_noop_without_env(monkeypatch: pytest.MonkeyPatch) -> None:
    """With neither fault env var set, the hook returns results unchanged and never stops."""
    results = results_for(make_attempts("tests-x/a-test:::A.B", 3))
    stop_calls = 0

    def fake_stop() -> None:
        nonlocal stop_calls
        stop_calls += 1

    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(stop=fake_stop))
    out = rts.maybe_inject_rerun_fault(1, results)
    assert out == results
    assert stop_calls == 0


def test_fault_hook_drops_results_on_first_submission_only(monkeypatch: pytest.MonkeyPatch) -> None:
    """YB_TEST_RERUN_DROP_RESULTS drops the last N results, and only on the first submission."""
    results = results_for(make_attempts("tests-x/a-test:::A.B", 10))
    monkeypatch.setenv("YB_TEST_RERUN_DROP_RESULTS", "4")

    kept = rts.maybe_inject_rerun_fault(1, results)
    assert kept == results[:6]

    # A later submission is never faulted, so recovery can complete.
    assert rts.maybe_inject_rerun_fault(2, results) == results


def test_fault_hook_stops_context(monkeypatch: pytest.MonkeyPatch) -> None:
    """YB_TEST_RERUN_STOP_CONTEXT stops the Spark context after the first submission."""
    results = results_for(make_attempts("tests-x/a-test:::A.B", 3))
    stop_calls = 0

    def fake_stop() -> None:
        nonlocal stop_calls
        stop_calls += 1

    monkeypatch.setenv("YB_TEST_RERUN_STOP_CONTEXT", "1")
    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(stop=fake_stop))

    out = rts.maybe_inject_rerun_fault(1, results)
    assert out == results          # STOP alone does not drop results
    assert stop_calls == 1
    # Not stopped again on later submissions.
    rts.maybe_inject_rerun_fault(2, results)
    assert stop_calls == 1


def test_fault_hooks_combine_to_drive_recovery(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    DROP + STOP together simulate a lost application: the first submission loses some results and
    the context is stopped, so the wrapper re-creates the context and re-submits the missing
    attempts, ending with every attempt accounted for.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []
    restart_count = 0

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return results_for(pending)

    def record_restart() -> None:
        nonlocal restart_count
        restart_count += 1

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(stop=lambda: None))
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setenv("YB_TEST_RERUN_DROP_RESULTS", "3")
    monkeypatch.setenv("YB_TEST_RERUN_STOP_CONTEXT", "1")

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True)

    assert len(submissions) == 2       # first submission + one recovery submission
    assert len(submissions[1]) == 3    # exactly the 3 dropped attempts are re-submitted
    assert restart_count == 1          # context re-created once before the recovery submission
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)
