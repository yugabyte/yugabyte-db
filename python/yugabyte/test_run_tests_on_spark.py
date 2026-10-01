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

import os
import sys
import threading
import time
import types
from typing import Any, List, Set

import pytest

# Import the module (not the TestDescriptor class) so pytest does not try to collect the
# Test*-named class as a test case.
from yugabyte import run_tests_on_spark as rts
from yugabyte import test_descriptor
from yugabyte import yb_dist_tests


# run_tests_job() is faked in every test here, so the conf is only threaded through, never used.
# It is a real TestConfig rather than a fake so that mypy checks the call sites; none of these
# paths are used.
FAKE_CONF = yb_dist_tests.TestConfig(
    build_root='/fake/yb-src-root/build/debug-clang17-dynamic-ninja',
    build_type='debug',
    yb_src_root='/fake/yb-src-root',
    archive_for_workers=None,
    rel_build_root='build/debug-clang17-dynamic-ninja',
    archive_sha256sum=None,
    compiler_type='clang17')

# A caller-supplied environment, distinct from the module-level propagated_env_vars, so a test can
# tell which one the wrapper actually forwards.
FAKE_ENV = {'YB_FAKE_ENV_MARKER': 'from-the-caller'}


def make_attempts(base: str, num_repetitions: int) -> List[test_descriptor.TestDescriptor]:
    """Expand one failed test into its per-attempt descriptors, the same way main() does."""
    base_descriptor = test_descriptor.TestDescriptor(base)
    return [base_descriptor.with_attempt_index(i) for i in range(1, num_repetitions + 1)]


def make_result(descriptor_str: str, exit_code: int = 0,
                skipped: bool = False) -> yb_dist_tests.TestResult:
    """A minimal TestResult carrying only the fields the re-run orchestration reads."""
    return yb_dist_tests.TestResult(
        test_descriptor=test_descriptor.TestDescriptor(descriptor_str),
        exit_code=exit_code,
        elapsed_time_sec=0.0,
        failed_without_output=False,
        artifact_paths=None,
        artifact_copy_result=None,
        spark_error_copy_result=None,
        skipped=skipped)


def results_for(
        descriptors: List[test_descriptor.TestDescriptor]) -> List[yb_dist_tests.TestResult]:
    return [make_result(td.descriptor_str) for td in descriptors]


def descriptor_strs(descriptors: List[test_descriptor.TestDescriptor]) -> Set[str]:
    return set(td.descriptor_str for td in descriptors)


@pytest.fixture(autouse=True)
def isolate_spark(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Neutralize the real Spark-touching helpers and reset the module-global cancellation flag
    before each test.
    """
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setattr(rts, "restart_spark_context", lambda conf: None)
    monkeypatch.setattr(rts, "g_cancelled_job_groups", set())
    # Ensure the test-only fault hooks are off, so the behavioral tests are not perturbed by a
    # developer running with these set in their environment.
    monkeypatch.delenv("YB_TEST_SUBMIT_DROP_RESULTS", raising=False)
    monkeypatch.delenv("YB_TEST_SUBMIT_STOP_CONTEXT", raising=False)


def test_all_attempts_complete_on_first_submission(monkeypatch: pytest.MonkeyPatch) -> None:
    """Happy path: every attempt returns a result, so there is exactly one submission."""
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

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
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # First submission loses the second half; the resubmission returns everything it is given.
        returned = list(pending)[:5] if len(submissions) == 1 else list(pending)
        return results_for(returned)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert len(submissions) == 2
    # Only the 5 attempts missing a result are resubmitted.
    assert len(submissions[1]) == 5
    # No completed attempt is resubmitted.
    assert set(submissions[1]).isdisjoint(set(submissions[0][:5]))
    # All 10 iterations end up with a result.
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_context_recreated_only_when_stopped(monkeypatch: pytest.MonkeyPatch) -> None:
    """restart_spark_context() is called before every submission iff the context is stopped."""
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    submissions: List[List[test_descriptor.TestDescriptor]] = []
    restart_count = 0

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        # Never return a result for the last attempt, forcing repeated resubmissions.
        return results_for([td for td in pending if td.attempt_index != 4])

    def record_restart(conf: yb_dist_tests.TestConfig) -> None:
        nonlocal restart_count
        restart_count += 1

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "SPARK_JOB_MAX_SUBMITS", 3)

    # Context reports stopped -> restart before each of the 3 submissions, including the first
    # (an already-dead context at rerun entry must be recovered before the first submission).
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})
    assert len(submissions) == 3
    assert restart_count == 3

    # Context reports alive -> never restart, even though resubmissions still happen.
    submissions.clear()
    restart_count = 0
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)
    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})
    assert len(submissions) == 3
    assert restart_count == 0


def test_dead_context_at_entry_is_restarted_before_first_submission(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Regression for the build where the main test job's Spark application was removed ("Master
    removed our application: FAILED"), so the rerun phase began with an already-stopped context.
    The context must be restarted BEFORE the first submission, so run_tests_job() never runs on a
    dead context (which previously raised an uncaught Py4JJavaError and crashed the rerun phase).
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    events: List[str] = []
    stopped = {"value": True}  # already dead when the rerun phase starts

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        events.append("submit")
        return results_for(pending)

    def record_restart(conf: yb_dist_tests.TestConfig) -> None:
        events.append("restart")
        stopped["value"] = False

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: stopped["value"])

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    # The restart happened before the first (and here only) submission.
    assert events == ["restart", "submit"]
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_submission_error_is_recovered_not_crashed(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    If run_tests_job() raises on a stopped context -- the symptom of parallelize()/accumulator()
    running outside run_spark_action()'s guard -- the rerun phase must treat the submission as
    producing no results and re-submit, not crash. Recovery here is driven by the context
    reporting stopped (the fixture default); py4j itself is not importable in the unit tests.
    """
    class Py4JJavaError(Exception):
        pass

    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    submissions = {"n": 0}

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        if submissions["n"] == 1:
            raise Py4JJavaError("Cannot call methods on a stopped SparkContext.")
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert submissions["n"] == 2  # first submission raised, second succeeded
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_failed_restart_is_retried_not_crashed(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    If the context is lost again right after recovery -- e.g. restart_spark_context() cannot reach
    the Spark master while the cluster is still churning -- that submission yields no results and
    the loop waits and retries. It must not crash the rerun phase.
    """
    class Py4JNetworkError(Exception):
        pass

    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    restarts = {"n": 0}
    submissions = {"n": 0}

    def flaky_restart(conf: yb_dist_tests.TestConfig) -> None:
        restarts["n"] += 1
        if restarts["n"] == 1:
            raise Py4JNetworkError("cannot connect to the Spark master")
        # A later restart succeeds.

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        return results_for(pending)

    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setattr(rts, "restart_spark_context", flaky_restart)
    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert restarts["n"] == 2      # first recovery failed, second succeeded
    assert submissions["n"] == 1   # run_tests_job only ran after a successful recovery
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_non_py4j_error_with_dead_context_is_retried(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A transient failure py4j does not wrap -- e.g. a socket/EOF error when the driver's gateway is
    gone -- must still be retried when the context is down. The retry decision is not py4j-name
    only: a stopped context is enough on its own.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    submissions = {"n": 0}

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        if submissions["n"] == 1:
            raise EOFError("gateway connection closed")
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)  # application is down

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert submissions["n"] == 2  # EOFError on a dead context is retried, not fatal
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_error_with_healthy_context_is_not_swallowed(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A failure while the context is healthy is a real bug, not a lost application, and must
    propagate rather than being retried and hidden.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        raise ValueError("unrelated bug")

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)  # context is fine

    with pytest.raises(ValueError, match="unrelated bug"):
        rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})


def test_cancellation_stops_resubmission(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    When the Spark job was cancelled after hitting the test-failure threshold, the wrapper must
    not fight that decision by resubmitting the (by-design) missing attempts.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        # Simulate run_spark_action detecting the deliberate cancellation.
        rts.g_cancelled_job_groups.add(rts.FAILED_TESTS_ON_DIFF_JOB_GROUP)
        return results_for(list(pending)[:3])

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert len(submissions) == 1
    assert len(results) == 3


def test_permanent_loss_exhausts_submission_budget(monkeypatch: pytest.MonkeyPatch) -> None:
    """If no results ever come back, the wrapper tries at most SPARK_JOB_MAX_SUBMITS times."""
    attempts = make_attempts("tests-x/a-test:::A.B", 10)
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return []

    monkeypatch.setattr(rts, "SPARK_JOB_MAX_SUBMITS", 5)
    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

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
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # Lose exactly one attempt of each test on the first pass.
        if len(submissions) == 1:
            return results_for([td for td in pending if td.attempt_index != 2])
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

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
    out = rts.maybe_inject_submit_fault(1, results)
    assert out == results
    assert stop_calls == 0


def test_fault_hook_drops_results_on_first_submission_only(monkeypatch: pytest.MonkeyPatch) -> None:
    """YB_TEST_SUBMIT_DROP_RESULTS drops the last N results, and only on the first submission."""
    results = results_for(make_attempts("tests-x/a-test:::A.B", 10))
    monkeypatch.setenv("YB_TEST_SUBMIT_DROP_RESULTS", "4")

    kept = rts.maybe_inject_submit_fault(1, results)
    assert kept == results[:6]

    # A later submission is never faulted, so recovery can complete.
    assert rts.maybe_inject_submit_fault(2, results) == results


def test_fault_hook_stops_context(monkeypatch: pytest.MonkeyPatch) -> None:
    """YB_TEST_SUBMIT_STOP_CONTEXT stops the Spark context after the first submission."""
    results = results_for(make_attempts("tests-x/a-test:::A.B", 3))
    stop_calls = 0

    def fake_stop() -> None:
        nonlocal stop_calls
        stop_calls += 1

    monkeypatch.setenv("YB_TEST_SUBMIT_STOP_CONTEXT", "1")
    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(stop=fake_stop))

    out = rts.maybe_inject_submit_fault(1, results)
    assert out == results          # STOP alone does not drop results
    assert stop_calls == 1
    # Not stopped again on later submissions.
    rts.maybe_inject_submit_fault(2, results)
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
    # The context is alive when the rerun phase starts; the STOP fault hook kills it after the
    # first submission, and restart_spark_context() brings it back. Tracking real state (rather
    # than a constant lambda) keeps the first-submission liveness check honest: it must NOT
    # restart on submission 1, only on the recovery submission.
    stopped = {"value": False}

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return results_for(pending)

    def fake_stop() -> None:
        stopped["value"] = True

    def record_restart(conf: yb_dist_tests.TestConfig) -> None:
        nonlocal restart_count
        restart_count += 1
        stopped["value"] = False

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(stop=fake_stop))
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: stopped["value"])
    monkeypatch.setenv("YB_TEST_SUBMIT_DROP_RESULTS", "3")
    monkeypatch.setenv("YB_TEST_SUBMIT_STOP_CONTEXT", "1")

    results = rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert len(submissions) == 2       # first submission + one recovery submission
    assert len(submissions[1]) == 3    # exactly the 3 dropped attempts are re-submitted
    assert restart_count == 1          # context re-created once before the recovery submission
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


# ---------------------------------------------------------------------------------------------
# What the wrapper forwards, and which attempts it re-submits.
# ---------------------------------------------------------------------------------------------

def test_caller_env_reaches_every_submission(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    The wrapper must hand its own env_vars parameter to each submission, including the ones after
    a re-submission. Taking the module-level propagated_env_vars instead would still work for the
    initial run and silently ignore a caller that wants a different environment - which is the
    whole point of passing it as a parameter.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    seen_envs: List[Any] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        seen_envs.append(env_vars)
        # Drop one attempt on the first submission so a second one happens.
        keep = [td for td in pending if td.attempt_index != 4] if not seen_envs[1:] else pending
        return results_for(keep)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    # A different value in the module global: if the wrapper reads that instead, this test fails.
    monkeypatch.setattr(rts, "propagated_env_vars", {'YB_FAKE_ENV_MARKER': 'from-the-global'})

    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

    assert len(seen_envs) == 2
    assert seen_envs == [FAKE_ENV, FAKE_ENV]


def test_initial_job_lost_application_is_resubmitted(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    The initial test job (rerun=False) is also submitted through the wrapper. When the whole Spark
    application is lost before any test produces a result (e.g. the shared cluster is being
    drained: "Master removed our application: FAILED"), the job must be re-submitted on a fresh
    context instead of ending the run with zero results.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 5)
    submissions: List[List[test_descriptor.TestDescriptor]] = []
    rerun_flags: List[bool] = []
    restart_count = 0
    # The context is alive for the first submission and dies with the application.
    stopped = {"value": False}

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        rerun_flags.append(rerun)
        if len(submissions) == 1:
            # The first submission loses the whole application: no results, dead context.
            stopped["value"] = True
            return []
        return results_for(pending)

    def record_restart(conf: yb_dist_tests.TestConfig) -> None:
        nonlocal restart_count
        restart_count += 1
        stopped["value"] = False

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: stopped["value"])

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=False, conf=FAKE_CONF, env_vars=FAKE_ENV)

    assert len(submissions) == 2
    assert rerun_flags == [False, False]   # the rerun flag is passed through unchanged
    assert restart_count == 1              # the dead context is re-created before resubmission
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_failed_attempts_are_not_resubmitted(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Mixed outcome on one submission: some attempts pass, some fail (non-zero exit code) and some
    are lost (no result). Only the lost attempts may be re-submitted; a ran-and-failed attempt
    already has its result and must flow to the failed-test re-run phase instead.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 6)
    submissions: List[List[str]] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            job_group: Any = None,
            shares_context: bool = False,
            stop_on_failure_limit: bool = True) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        if len(submissions) == 1:
            # Attempts 1-2 pass, 3-4 fail, 5-6 are lost with the application.
            return [make_result(td.descriptor_str, exit_code=int(td.attempt_index in (3, 4)))
                    for td in pending if td.attempt_index <= 4]
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=False, conf=FAKE_CONF, env_vars=FAKE_ENV)

    assert len(submissions) == 2
    # Only the two lost attempts are re-submitted; the failed ones are not.
    assert set(submissions[1]) == {
        "tests-x/a-test:::A.B:::attempt_5",
        "tests-x/a-test:::A.B:::attempt_6",
    }
    # Every attempt has exactly one result, and the failed ones keep their failed result.
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)
    assert {r.test_descriptor.descriptor_str for r in results if r.exit_code != 0} == {
        "tests-x/a-test:::A.B:::attempt_3",
        "tests-x/a-test:::A.B:::attempt_4",
    }


# ---------------------------------------------------------------------------------------------
# Cancelled vs lost: run_spark_action decides which, and the callers do the opposite on each.
# ---------------------------------------------------------------------------------------------

@pytest.fixture
def fake_py4j(monkeypatch: pytest.MonkeyPatch) -> Any:
    """
    run_spark_action imports py4j to catch Py4JJavaError, and py4j is absent here (it only arrives
    with pyspark). Stand in a module whose exception type we can raise.
    """
    class Py4JJavaError(Exception):
        pass

    protocol = types.ModuleType('py4j.protocol')
    protocol.Py4JJavaError = Py4JJavaError  # type: ignore[attr-defined]
    py4j = types.ModuleType('py4j')
    py4j.protocol = protocol  # type: ignore[attr-defined]
    monkeypatch.setitem(sys.modules, 'py4j', py4j)
    monkeypatch.setitem(sys.modules, 'py4j.protocol', protocol)
    return Py4JJavaError


# Taken from a Spark 3.5 local[2] run that reached the failure threshold - once with a single job
# (cancelAllJobs) and once with two concurrent jobs (cancelJobGroup). Pinning the real messages
# rather than parametrizing over SPARK_JOB_CANCELLED_MESSAGES is the point: a test built from that
# tuple can only confirm the strings someone already chose, and would keep passing if an edit
# stopped it matching what Spark actually emits.
OBSERVED_SPARK_CANCELLATION_MESSAGES = (
    "org.apache.spark.SparkException: Job aborted due to stage failure: Task 0 in stage 0.0 failed "
    "1 times, most recent failure: Lost task 0.0 in stage 0.0 (TID 0) (host executor driver): "
    "TaskKilled (Stage cancelled: Job 0 cancelled as part of cancellation of all jobs)",
    "org.apache.spark.SparkException: Job aborted due to stage failure: Task 0 in stage 1.0 failed "
    "1 times, most recent failure: Lost task 0.0 in stage 1.0 (TID 4) (host executor driver): "
    "TaskKilled (Stage cancelled: Job 1 cancelled part of cancelled job group diff-rerun)",
)


class FakeAccumulator:
    def __init__(self, value: Any) -> None:
        self.value = value


class FakeStatusTracker:
    """Answers getJobIdsForGroup the way the test asks it to - a list, or an exception."""

    def __init__(self, answer: Any) -> None:
        self.answer = answer
        self.asked: List[str] = []

    def getJobIdsForGroup(self, job_group: str) -> Any:  # noqa: N802 - Spark's spelling
        self.asked.append(job_group)
        if isinstance(self.answer, Exception):
            raise self.answer
        return self.answer


class FakeRDD:
    def map(self, func: Any) -> 'FakeRDD':
        return self

    def collect(self) -> list:
        # Long enough for the monitor thread to reach its decision, short enough not to matter.
        time.sleep(0.5)
        return []


class FakeSparkContext:
    """
    Enough of a SparkContext for run_tests_job's monitor to run: the failure count starts above
    the limit, so monitor_fail_count skips its polling loop and decides immediately.
    """
    applicationId = 'local-fake'

    def __init__(self, group_job_ids: Any) -> None:
        self.statusTrackerObj = FakeStatusTracker(group_job_ids)
        self.cancelled_groups: List[str] = []
        self.cancel_all_calls = 0

    def setJobGroup(self, group_id: str, description: str) -> None:  # noqa: N802
        pass

    def accumulator(self, value: Any, accum_param: Any = None) -> FakeAccumulator:
        # The list accumulator collects results; the int one is the failure count, and it is
        # already over the limit this test sets.
        return FakeAccumulator([] if accum_param is not None else 99)

    def parallelize(self, values: Any, numSlices: int = 1) -> FakeRDD:  # noqa: N803
        return FakeRDD()

    def statusTracker(self) -> FakeStatusTracker:  # noqa: N802
        return self.statusTrackerObj

    def cancelJobGroup(self, job_group: str) -> None:  # noqa: N802
        self.cancelled_groups.append(job_group)

    def cancelAllJobs(self) -> None:  # noqa: N802
        self.cancel_all_calls += 1


@pytest.fixture
def fake_pyspark(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    run_tests_job imports pyspark.accumulators for its list accumulator, and pyspark is absent
    here - the same reason fake_py4j exists. The fake context never uses the param, so an empty
    base class is enough.
    """
    class AccumulatorParam:
        pass

    accumulators = types.ModuleType('pyspark.accumulators')
    accumulators.AccumulatorParam = AccumulatorParam  # type: ignore[attr-defined]
    pyspark = types.ModuleType('pyspark')
    pyspark.accumulators = accumulators  # type: ignore[attr-defined]
    monkeypatch.setitem(sys.modules, 'pyspark', pyspark)
    monkeypatch.setitem(sys.modules, 'pyspark.accumulators', accumulators)


def run_job_reaching_the_limit(
        monkeypatch: pytest.MonkeyPatch, group_job_ids: Any) -> FakeSparkContext:
    spark_context = FakeSparkContext(group_job_ids)
    monkeypatch.setattr(rts, "spark_context", spark_context)
    monkeypatch.setattr(rts, "g_max_num_test_failures", 3)
    monkeypatch.setattr(rts, "g_cancelled_job_groups", set())
    rts.run_tests_job(
        [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")],
        rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV,
        job_group=rts.FAILED_TESTS_ON_DIFF_JOB_GROUP, shares_context=True)
    return spark_context


def test_the_failure_limit_cancels_only_its_own_job_group(
        monkeypatch: pytest.MonkeyPatch, fake_pyspark: None, fake_py4j: Any) -> None:
    """The point of the groups: one job reaching the limit must not stop the one beside it."""
    spark_context = run_job_reaching_the_limit(monkeypatch, [7])
    assert spark_context.cancelled_groups == [rts.FAILED_TESTS_ON_DIFF_JOB_GROUP]
    assert spark_context.cancel_all_calls == 0


def test_an_empty_job_group_falls_back_to_cancelling_everything(
        monkeypatch: pytest.MonkeyPatch, fake_pyspark: None, fake_py4j: Any,
        caplog: Any) -> None:
    """
    Reaching this point means the failure count crossed the limit, so the job is certainly
    submitted and an empty group can only mean the group was never applied - pinned-thread mode
    off, where setJobGroup and the submission land on different JVM threads. cancelJobGroup would
    then match nothing and the limit would stop nothing at all, so everything is cancelled
    instead: the best-effort baseline sample is traded for a limit that still works.

    Only reachable here. Turning PYSPARK_PIN_THREAD off does not reproduce it on a local master -
    py4j's classic gateway still keeps one connection per python thread, so the groups land
    correctly anyway.
    """
    with caplog.at_level('WARNING'):
        spark_context = run_job_reaching_the_limit(monkeypatch, [])
    assert spark_context.cancelled_groups == []
    assert spark_context.cancel_all_calls == 1
    assert 'job groups are not being applied' in caplog.text


def test_a_status_tracker_that_raises_falls_back_the_same_way(
        monkeypatch: pytest.MonkeyPatch, fake_pyspark: None, fake_py4j: Any,
        caplog: Any) -> None:
    """
    An unknown group comes back as an empty list rather than an exception - confirmed against
    Spark 3.5 in both thread modes - so this is defence against a future version, not the
    observed behaviour. It must not leave the limit doing nothing.
    """
    with caplog.at_level('ERROR'):
        spark_context = run_job_reaching_the_limit(monkeypatch, RuntimeError("no such group"))
    assert spark_context.cancelled_groups == []
    assert spark_context.cancel_all_calls == 1
    assert 'Could not list the jobs of group' in caplog.text


def test_requested_cancellation_is_recognized_regardless_of_wording(
        monkeypatch: pytest.MonkeyPatch, fake_py4j: Any) -> None:
    """
    The authoritative signal: monitor_fail_count set cancel_requested before cancelling, so the
    flag must be set no matter how Spark words the resulting error - this is what frees the
    threshold from tracking wording across Spark versions. The message here matches nothing in
    SPARK_JOB_CANCELLED_MESSAGES on purpose.
    """
    def action() -> None:
        raise fake_py4j("Job 1 was axed for reasons Spark 4.x words in some brand-new way")

    cancel_requested = threading.Event()
    cancel_requested.set()
    monkeypatch.setattr(rts, "g_cancelled_job_groups", set())
    assert rts.run_spark_action(
        action, rts.FAILED_TESTS_ON_DIFF_JOB_GROUP, cancel_requested) is None
    assert rts.FAILED_TESTS_ON_DIFF_JOB_GROUP in rts.g_cancelled_job_groups


@pytest.mark.parametrize('message', list(OBSERVED_SPARK_CANCELLATION_MESSAGES))
def test_external_cancellation_is_recognized_by_wording(
        monkeypatch: pytest.MonkeyPatch, fake_py4j: Any, message: str) -> None:
    """
    A cancellation we did not request (cancel_requested unset - e.g. an operator killing the job
    from the Spark UI) is only recognizable by Spark's wording. It must still set the flag:
    re-submitting would fight whoever cancelled the job. cancelAllJobs() and cancelJobGroup() word
    it differently - the latter as "cancelled part of", not "as part of" - hence one case each.
    """
    def action() -> None:
        raise fake_py4j(message)

    monkeypatch.setattr(rts, "g_cancelled_job_groups", set())
    assert rts.run_spark_action(
        action, rts.FAILED_TESTS_ON_DIFF_JOB_GROUP, threading.Event()) is None
    assert rts.FAILED_TESTS_ON_DIFF_JOB_GROUP in rts.g_cancelled_job_groups


@pytest.mark.parametrize('message', [
    "Master removed our application: FAILED",
    # Spark words a stopped context with "cancelled" too (DAGScheduler's cleanUpAfterSchedulerStop);
    # it must still classify as lost so the re-submission recovery runs. This is why the match
    # requires the cancel APIs' reason text and not just the word "cancelled".
    "Job 1 cancelled because SparkContext was shut down",
])
def test_other_spark_error_is_not_treated_as_cancellation(
        monkeypatch: pytest.MonkeyPatch, fake_py4j: Any, message: str) -> None:
    """A lost application must stay 'lost', so the attempts get re-submitted."""
    def action() -> None:
        raise fake_py4j(message)

    monkeypatch.setattr(rts, "g_cancelled_job_groups", set())
    assert rts.run_spark_action(
        action, rts.FAILED_TESTS_ON_DIFF_JOB_GROUP, threading.Event()) is None
    assert rts.FAILED_TESTS_ON_DIFF_JOB_GROUP not in rts.g_cancelled_job_groups


# ---------------------------------------------------------------------------------------------
# The failed-tests-on-baseline job runs inside the re-submission loop.
# ---------------------------------------------------------------------------------------------

def test_baseline_phase_is_resubmitted_after_context_loss(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    With a baseline conf, each submission is the two-job concurrent phase. Losing the application
    must re-submit that phase - and the full baseline set goes again, because the previous
    attempt's baseline results went down with the context.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")]
    calls: List[dict] = []
    stopped = {"value": False}

    def fake_phase(diff_rerun_descriptors: List[test_descriptor.TestDescriptor],
                   baseline_test_descriptors: List[test_descriptor.TestDescriptor],
                   conf: Any, baseline_conf: Any, env_vars: Any,
                   args: Any) -> List[yb_dist_tests.TestResult]:
        calls.append({'diff': list(diff_rerun_descriptors),
                      'baseline': list(baseline_test_descriptors)})
        if len(calls) == 1:
            stopped["value"] = True          # the application is lost after the first submission
            return results_for(diff_rerun_descriptors[:2])
        return results_for(diff_rerun_descriptors)

    monkeypatch.setattr(rts, "rerun_failed_tests_on_diff_and_baseline", fake_phase)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: stopped["value"])
    monkeypatch.setattr(rts, "restart_spark_context",
                        lambda conf: stopped.__setitem__("value", False))

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars={}, baseline_conf=FAKE_CONF,
        baseline_test_descriptors=baseline_tests, args=types.SimpleNamespace())

    assert len(calls) == 2
    assert len(calls[1]['diff']) == 2                      # only the diff attempts still missing
    assert calls[1]['baseline'] == baseline_tests          # the whole baseline set, again
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


def test_without_baseline_conf_the_plain_job_is_used(monkeypatch: pytest.MonkeyPatch) -> None:
    """No baseline conf must leave the single-job path exactly as it was."""
    attempts = make_attempts("tests-x/a-test:::A.B", 2)
    used = {"phase": 0, "plain": 0}

    def fake_phase(*a: Any, **kw: Any) -> List[yb_dist_tests.TestResult]:
        used["phase"] += 1
        return []

    def fake_plain(pending: List[test_descriptor.TestDescriptor], rerun: bool, conf: Any,
                   env_vars: Any = None, job_group: Any = None,
                   shares_context: bool = False) -> List[yb_dist_tests.TestResult]:
        used["plain"] += 1
        return results_for(pending)

    monkeypatch.setattr(rts, "rerun_failed_tests_on_diff_and_baseline", fake_phase)
    monkeypatch.setattr(rts, "run_tests_job", fake_plain)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)

    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars={})

    assert used == {"phase": 0, "plain": 1}


# ---------------------------------------------------------------------------------------------
# Running each test on the baseline once per (base commit, lane).
# ---------------------------------------------------------------------------------------------

def test_tests_already_run_on_baseline_are_dropped_ignoring_attempt_index(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    CSI stores a test's uniqueId as its descriptor without the attempt index, so the comparison
    has to strip that index - otherwise nothing ever matches and every test is run again.
    """
    kept = test_descriptor.TestDescriptor("tests-x/a-test:::A.Keep")
    dropped = test_descriptor.TestDescriptor("tests-x/a-test:::A.Drop")
    monkeypatch.setattr(rts.csi_report, "test_ids_in_launches",
                        lambda attr_filter: {dropped.descriptor_str_without_attempt_index})

    remaining = rts.filter_already_run_on_baseline(
        [kept.with_attempt_index(1), dropped.with_attempt_index(1)], 'deadbeef')

    assert [td.descriptor_str_without_attempt_index for td in remaining] == \
        [kept.descriptor_str_without_attempt_index]


def test_csi_failure_runs_everything_on_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """A CSI outage may waste cluster time, but must never silently skip running on the baseline."""
    def boom(attr_filter: str) -> Set[str]:
        raise RuntimeError("CSI unreachable")

    monkeypatch.setattr(rts.csi_report, "test_ids_in_launches", boom)
    descriptors = make_attempts("tests-x/a-test:::A.B", 3)
    assert rts.filter_already_run_on_baseline(descriptors, 'deadbeef') == descriptors


def test_no_commit_id_skips_the_lookup(monkeypatch: pytest.MonkeyPatch) -> None:
    """Without a base commit there is nothing to deduplicate against, and no query to make."""
    def fail(attr_filter: str) -> Set[str]:
        raise AssertionError("must not query CSI without a commit id")

    monkeypatch.setattr(rts.csi_report, "test_ids_in_launches", fail)
    descriptors = make_attempts("tests-x/a-test:::A.B", 2)
    assert rts.filter_already_run_on_baseline(descriptors, None) == descriptors


# ---------------------------------------------------------------------------------------------
# Reading the baseline build's recorded Maven repo, and reporting the outcome.
# ---------------------------------------------------------------------------------------------

def test_recorded_mvn_repo_is_preferred(tmp_path: Any) -> None:
    """The build recorded which repo it used; we did not configure that build, so we read it."""
    (tmp_path / 'mvn_repo').write_text('/recorded/m2_repository\n')
    assert rts.get_mvn_local_repo(str(tmp_path)) == '/recorded/m2_repository'


@pytest.mark.parametrize('contents', [None, '', '   \n'])
def test_missing_or_empty_mvn_repo_record_falls_back(tmp_path: Any, contents: Any) -> None:
    """
    A build that skipped its Java build leaves no usable record - and no populated repo either.
    Falling back keeps the caller going as far as the archive step, whose validation reports it.
    """
    if contents is not None:
        (tmp_path / 'mvn_repo').write_text(contents)
    assert rts.get_mvn_local_repo(str(tmp_path)) == str(tmp_path / 'm2_repository')


def test_recorded_thirdparty_is_read(tmp_path: Any) -> None:
    """
    Whatever the build recorded, one entry per env var its record file names - the URLs along with
    the directory. A worker without the directory downloads it, but only from the URL:
    find_or_download_thirdparty leaves a missing YB_THIRDPARTY_DIR missing unless
    YB_THIRDPARTY_URL points somewhere.
    """
    (tmp_path / 'thirdparty_path.txt').write_text('/opt/yb-build/thirdparty/v1-clang17\n')
    (tmp_path / 'thirdparty_url.txt').write_text('https://example.invalid/v1-clang17.tar.gz\n')
    (tmp_path / 'thirdparty_checksum_url.txt').write_text(
        'https://example.invalid/v1-clang17.tar.gz.sha256\n')
    assert rts.get_thirdparty_env_vars(str(tmp_path)) == {
        'YB_THIRDPARTY_DIR': '/opt/yb-build/thirdparty/v1-clang17',
        'YB_THIRDPARTY_URL': 'https://example.invalid/v1-clang17.tar.gz',
        'YB_THIRDPARTY_CHECKSUM_URL': 'https://example.invalid/v1-clang17.tar.gz.sha256',
    }


def test_the_toolchain_is_left_to_the_diff_tree(tmp_path: Any) -> None:
    """
    A build records its toolchain the same way, but nothing on a worker fetches one, so naming the
    baseline's would leave a path nothing brings there. What the tests read from a toolchain is
    llvm-symbolizer, looked up under YB_THIRDPARTY_DIR first, and that does point at the baseline's.
    """
    (tmp_path / 'llvm_path.txt').write_text('/opt/yb-build/llvm/yb-llvm-v17\n')
    (tmp_path / 'llvm_url.txt').write_text('https://example.invalid/yb-llvm-v17.tar.gz\n')
    (tmp_path / 'gcc_path.txt').write_text('/opt/yb-build/gcc/yb-gcc-v13\n')
    assert rts.get_thirdparty_env_vars(str(tmp_path)) == {}


def test_unrecorded_thirdparty_is_absent(tmp_path: Any) -> None:
    """
    A build that had its thirdparty locally records no URL to download it by, and an empty value is
    not written at all. Reporting a key we have no value for would be worse than omitting it: the
    caller uses these to overwrite the diff tree's, so a '' would point the worker at nothing.
    """
    (tmp_path / 'thirdparty_url.txt').write_text('   \n')
    assert rts.get_thirdparty_env_vars(str(tmp_path)) == {}
    assert rts.get_thirdparty_env_vars(str(tmp_path / 'no-such-build-root')) == {}


def test_thirdparty_path_is_not_checked_against_this_host(tmp_path: Any) -> None:
    """
    The value is for the Spark worker, which downloads a thirdparty directory it does not have. It
    need not exist here - the baseline tree was built on another host - so reading it must not go
    through build_paths.BuildPaths, which requires that it does.
    """
    recorded = str(tmp_path / 'not-on-this-host')
    (tmp_path / 'thirdparty_path.txt').write_text(recorded + '\n')
    assert not os.path.exists(recorded)
    assert rts.get_thirdparty_env_vars(str(tmp_path)) == {'YB_THIRDPARTY_DIR': recorded}


def test_baseline_status_is_written(tmp_path: Any) -> None:
    status_file = tmp_path / 'status.txt'
    rts.write_baseline_status(str(status_file), 'done-partial-3-of-10')
    assert status_file.read_text() == 'done-partial-3-of-10\n'


def test_baseline_status_without_a_file_is_a_no_op() -> None:
    """The status file is optional; reporting must never be the thing that fails a run."""
    rts.write_baseline_status(None, 'done')
    rts.write_baseline_status('', 'done')


# ---------------------------------------------------------------------------------------------
# The conf derived from a build root, which the extraction path depends on.
# ---------------------------------------------------------------------------------------------

def test_conf_rejects_a_build_root_that_is_not_a_flavor_dir() -> None:
    """
    The build root's basename is parsed for build type and compiler, so a path that is not a
    <flavor> directory is rejected. That parse - not the relative-path check below - is what makes
    a wrong --baseline_build_root fail early.
    """
    with pytest.raises(ValueError, match='Too few components'):
        yb_dist_tests.make_conf_for_build_root(
            '/a/flavor', send_archive_to_workers=False,
            archive_name=yb_dist_tests.ARCHIVE_FOR_WORKERS_NAME)


def test_conf_does_not_validate_the_build_root_depth() -> None:
    """
    Pins a limit worth knowing: yb_src_root is *derived* as the build root's grandparent, so the
    relative path is always two components and the check on it cannot catch a build root at the
    wrong depth. '/a/b/c/build/x/<flavor>' is accepted with yb_src_root='/a/b/c/build'. What
    protects the baseline path is therefore the flavor parse above, the pipeline's workspace-root
    containment guard, and the preflight - not this check.
    """
    conf = yb_dist_tests.make_conf_for_build_root(
        '/a/b/c/build/x/debug-clang17-dynamic-ninja', send_archive_to_workers=False,
        archive_name=yb_dist_tests.ARCHIVE_FOR_WORKERS_NAME)
    assert conf.yb_src_root == '/a/b/c/build'
    assert conf.rel_build_root == 'x/debug-clang17-dynamic-ninja'


# ---------------------------------------------------------------------------------------------
# Worker-side name resolution.
# ---------------------------------------------------------------------------------------------

def test_no_function_shadows_a_module_level_import() -> None:
    """
    A function-level "from yugabyte import yb_dist_tests" makes that name local for the *whole*
    function body, so any use of it earlier in the function raises UnboundLocalError. An earlier
    iteration of this work rebuilt the conf at the top of parallel_run_test() and hit exactly that,
    failing every Spark task; the conf now arrives as an object and the parse is gone, but the
    shadowing import it tripped over is the kind of thing that comes back. Python decides
    local-vs-global at compile time, so no amount of mocking would catch it - only running a real
    task would. This check does, statically.
    """
    module_names = {
        name for name, value in vars(rts).items() if isinstance(value, types.ModuleType)
    }
    offenders = {
        name: sorted(module_names.intersection(value.__code__.co_varnames))
        for name, value in vars(rts).items()
        if isinstance(value, types.FunctionType) and
        module_names.intersection(value.__code__.co_varnames)
    }
    assert not offenders, (
        "These functions bind a name that is already a module-level import, shadowing it for the "
        "whole function body: %s" % offenders)


# ---------------------------------------------------------------------------------------------
# A CSI failure must not take the diff's re-runs down with the failed-tests-on-baseline job.
# ---------------------------------------------------------------------------------------------

def test_diff_rerun_survives_a_failure_setting_up_the_baseline_suites(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any) -> None:
    """
    The failed-tests-on-baseline job reports to CSI and is useless without it, but the diff's
    re-runs decide the build result. So a failure creating the baseline's CSI suites drops that job
    and runs
    the re-run phase alone, recording why - it must not propagate, which is what killed a run
    outright before: an unreachable CSI took the whole re-run phase with it.
    """
    diff_attempts = make_attempts("tests-x/a-test:::A.B", 2)
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")]
    status_file = tmp_path / "baseline_status.txt"
    jobs: List[dict] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'descriptors': list(test_descriptors), 'job_group': job_group})
        return results_for(test_descriptors)

    def exploding_launch_qid(launch: str) -> str:
        raise RuntimeError("CSI unreachable")

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts.csi_report, "launch_qid", exploding_launch_qid)

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(status_file), baseline_test_list_file=None,
        baseline_commit_id=None)
    results = rts.rerun_failed_tests_on_diff_and_baseline(
        diff_rerun_descriptors=diff_attempts,
        baseline_test_descriptors=baseline_tests,
        conf=FAKE_CONF,
        baseline_conf=FAKE_CONF,
        env_vars=FAKE_ENV,
        args=args)

    # One job only - the diff's - and it carries every attempt.
    assert len(jobs) == 1
    assert descriptor_strs(jobs[0]['descriptors']) == descriptor_strs(diff_attempts)
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(diff_attempts)
    assert status_file.read_text().strip() == 'skip-csi-setup-failed'


def test_diff_rerun_survives_a_rejected_baseline_suite(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any) -> None:
    """
    CSI reports a rejected create by returning an empty suite id, not by raising. An empty id
    reaches the workers as an empty YB_CSI_<language>, where create_test reports nothing - so the
    baseline tests would run and record nothing, and the outcome would still read 'done'. Treat it
    like the raising failures: drop the job, say why, and let a re-run measure what still fails.
    """
    diff_attempts = make_attempts("tests-x/a-test:::A.B", 2)
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")]
    status_file = tmp_path / "baseline_status.txt"
    jobs: List[dict] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'descriptors': list(test_descriptors), 'job_group': job_group})
        return results_for(test_descriptors)

    def rejected_create_suite(**kwargs: Any) -> Any:
        return ('YB_CSI_' + kwargs['suite_name'], '')

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts.csi_report, "launch_qid", lambda launch: '4242')
    monkeypatch.setattr(rts.csi_report, "create_suite", rejected_create_suite)
    # The empty id only means "rejected" when there is a server to reject it.
    monkeypatch.setenv("CSI_SERVER", "http://csi.example")
    monkeypatch.setenv("CSI_TOKEN", "token")

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(status_file), baseline_test_list_file=None,
        baseline_commit_id=None)
    results = rts.rerun_failed_tests_on_diff_and_baseline(
        diff_rerun_descriptors=diff_attempts,
        baseline_test_descriptors=baseline_tests,
        conf=FAKE_CONF,
        baseline_conf=FAKE_CONF,
        env_vars=FAKE_ENV,
        args=args)

    assert len(jobs) == 1
    assert jobs[0]['job_group'] == rts.FAILED_TESTS_ON_DIFF_JOB_GROUP
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(diff_attempts)
    assert status_file.read_text().strip() == 'skip-csi-setup-failed'


def test_a_launch_csi_cannot_resolve_drops_the_baseline_job(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any, caplog: Any) -> None:
    """
    launch_qid logs and returns '' for a launch that is not there, rather than raising - and this
    server then accepts create_suite against that same uuid, returning 201 for a launch no query
    will ever match. Left alone the run reads `done` over a sample recorded nowhere. The empty qid
    is the only signal, so it has to drop the job before any suite exists.
    """
    diff_attempts = make_attempts("tests-x/a-test:::A.B", 2)
    status_file = tmp_path / "baseline_status.txt"
    jobs: List[dict] = []
    created: List[str] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'descriptors': list(test_descriptors), 'job_group': job_group})
        return results_for(test_descriptors)

    def accepting_create_suite(**kwargs: Any) -> Any:
        # What the real server does with a launch uuid it does not know: 201 and an id.
        created.append(kwargs['suite_name'])
        return ('YB_CSI_' + kwargs['suite_name'], 'a-suite-id')

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts.csi_report, "launch_qid", lambda launch: '')
    monkeypatch.setattr(rts.csi_report, "create_suite", accepting_create_suite)
    monkeypatch.setenv("CSI_SERVER", "http://csi.example")
    monkeypatch.setenv("CSI_TOKEN", "token")

    args = types.SimpleNamespace(
        baseline_csi_launch='not-a-launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(status_file), baseline_test_list_file=None,
        baseline_commit_id=None)
    with caplog.at_level('ERROR'):
        results = rts.rerun_failed_tests_on_diff_and_baseline(
            diff_rerun_descriptors=diff_attempts,
            baseline_test_descriptors=[test_descriptor.TestDescriptor("tests-x/a-test:::A.B")],
            conf=FAKE_CONF,
            baseline_conf=FAKE_CONF,
            env_vars=FAKE_ENV,
            args=args)

    assert status_file.read_text().strip() == 'skip-csi-setup-failed'
    assert 'could not resolve the baseline launch' in caplog.text
    # Dropped before any suite existed, so there is nothing left IN_PROGRESS in a shared launch.
    assert created == []
    assert len(jobs) == 1
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(diff_attempts)


def test_a_suite_accepted_before_the_refused_one_is_closed(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any) -> None:
    """
    The refusal can come on the second language, after the first suite was created. Bailing without
    closing it would leave it IN_PROGRESS in the commit-scoped launch, which is shared with every
    peer diff on this base commit - and the pipeline's launch finish would then mark it INTERRUPTED.
    """
    diff_attempts = make_attempts("tests-x/a-test:::A.B", 2)
    # Two languages, so there is an accepted suite to leak before the refused one gives up.
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B"),
                      test_descriptor.TestDescriptor("com.yugabyte.jedis.TestYBJedis#testPool")]
    assert [d.language for d in baseline_tests] == ['C++', 'Java']
    status_file = tmp_path / "baseline_status.txt"
    closed: List[str] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        return results_for(test_descriptors)

    def create_suite_refusing_java(**kwargs: Any) -> Any:
        varname = 'YB_CSI_' + kwargs['suite_name']
        if kwargs['suite_name'] == 'Java':
            return (varname, '')
        return (varname, 'cxx-suite-id')

    def fake_close_item(item: str, time_sec: float, status: str, tags: Any,
                        launch: str = '') -> str:
        closed.append(item)
        return ''

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts.csi_report, "launch_qid", lambda launch: '4242')
    monkeypatch.setattr(rts.csi_report, "create_suite", create_suite_refusing_java)
    monkeypatch.setattr(rts.csi_report, "close_item", fake_close_item)
    monkeypatch.setenv("CSI_SERVER", "http://csi.example")
    monkeypatch.setenv("CSI_TOKEN", "token")

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(status_file), baseline_test_list_file=None,
        baseline_commit_id=None)
    rts.rerun_failed_tests_on_diff_and_baseline(
        diff_rerun_descriptors=diff_attempts,
        baseline_test_descriptors=baseline_tests,
        conf=FAKE_CONF,
        baseline_conf=FAKE_CONF,
        env_vars=FAKE_ENV,
        args=args)

    assert status_file.read_text().strip() == 'skip-csi-setup-failed'
    assert 'cxx-suite-id' in closed


def test_baseline_still_runs_when_csi_is_not_configured(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any) -> None:
    """
    Without CSI_SERVER/CSI_TOKEN every suite id is empty by design and nothing consumes the launch,
    so an empty id there is not a rejection. Local runs must still get both jobs - this is how the
    two-job phase is exercised outside Jenkins.
    """
    diff_attempts = make_attempts("tests-x/a-test:::A.B", 2)
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")]
    status_file = tmp_path / "baseline_status.txt"
    jobs: List[dict] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'descriptors': list(test_descriptors), 'job_group': job_group,
                     'env_vars': dict(env_vars),
                     'stop_on_failure_limit': stop_on_failure_limit})
        return results_for(test_descriptors)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    # No head start to wait through: the ordering it gives is irrelevant to what this asserts.
    monkeypatch.setattr(rts, "BASELINE_SUBMIT_ORDER_DELAY_SEC", 0)
    # The real csi_report functions are used, and all of them return before any HTTP when
    # not configured - which is the state under test.
    monkeypatch.delenv("CSI_SERVER", raising=False)
    monkeypatch.delenv("CSI_TOKEN", raising=False)

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(status_file), baseline_test_list_file=None,
        baseline_commit_id=None)
    rts.rerun_failed_tests_on_diff_and_baseline(
        diff_rerun_descriptors=diff_attempts,
        baseline_test_descriptors=baseline_tests,
        conf=FAKE_CONF,
        baseline_conf=FAKE_CONF,
        env_vars=FAKE_ENV,
        args=args)

    assert sorted(job['job_group'] for job in jobs) == sorted(
        [rts.FAILED_TESTS_ON_DIFF_JOB_GROUP, rts.FAILED_TESTS_ON_BASELINE_JOB_GROUP])
    assert status_file.read_text().strip() == 'done'
    # The caller's env reaches both jobs, the same guarantee
    # test_caller_env_reaches_every_submission makes for the single-job path. The baseline job
    # additionally gets its own CSI vars on top.
    for job in jobs:
        assert job['env_vars']['YB_FAKE_ENV_MARKER'] == FAKE_ENV['YB_FAKE_ENV_MARKER']
    baseline_job = next(job for job in jobs
                        if job['job_group'] == rts.FAILED_TESTS_ON_BASELINE_JOB_GROUP)
    assert baseline_job['env_vars']['YB_CSI_LID'] == 'launch-uuid'

    # The failure limit exists to stop a job whose failures mean something is wrong. The
    # failed tests on the baseline are the opposite: they already failed on the diff, and how many
    # of them also fail on the base commit is the measurement, so a base commit bad enough to reach
    # the limit would truncate exactly the sample that shows it. That job therefore runs
    # without that limit, while the diff's re-runs keep it.
    diff_job = next(job for job in jobs
                    if job['job_group'] == rts.FAILED_TESTS_ON_DIFF_JOB_GROUP)
    assert baseline_job['stop_on_failure_limit'] is False
    assert diff_job['stop_on_failure_limit'] is True


def test_baseline_job_runs_under_the_baseline_tree_s_thirdparty(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any, caplog: Any) -> None:
    """
    propagate_env_vars() hands both jobs the diff tree's YB_THIRDPARTY_DIR, and on the worker
    find_or_download_thirdparty stops at that variable without consulting the build root's own
    record. So the baseline job has to be given the baseline tree's value here, or its tests run
    against the diff's thirdparty - and its binaries were linked against its own, not the diff's.
    """
    baseline_root = tmp_path / 'baseline-build-root'
    baseline_root.mkdir()
    (baseline_root / 'thirdparty_path.txt').write_text('/opt/yb-build/thirdparty/baseline\n')
    (baseline_root / 'thirdparty_url.txt').write_text(
        'https://example.invalid/baseline-tp.tar.gz\n')
    baseline_conf = yb_dist_tests.TestConfig(
        build_root=str(baseline_root),
        build_type=FAKE_CONF.build_type,
        yb_src_root=str(tmp_path),
        archive_for_workers=None,
        rel_build_root='build/baseline-build-root',
        archive_sha256sum=None,
        compiler_type=FAKE_CONF.compiler_type)
    diff_env = dict(FAKE_ENV)
    diff_env['YB_THIRDPARTY_DIR'] = '/opt/yb-build/thirdparty/diff'
    diff_env['YB_THIRDPARTY_URL'] = 'https://example.invalid/diff-tp.tar.gz'
    diff_env['YB_LLVM_TOOLCHAIN_DIR'] = '/opt/yb-build/llvm/diff'
    jobs: List[dict] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'job_group': job_group, 'env_vars': dict(env_vars)})
        return results_for(test_descriptors)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "BASELINE_SUBMIT_ORDER_DELAY_SEC", 0)
    monkeypatch.delenv("CSI_SERVER", raising=False)
    monkeypatch.delenv("CSI_TOKEN", raising=False)

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(tmp_path / 'baseline_status.txt'), baseline_test_list_file=None,
        baseline_commit_id=None)
    with caplog.at_level('INFO'):
        rts.rerun_failed_tests_on_diff_and_baseline(
            diff_rerun_descriptors=make_attempts("tests-x/a-test:::A.B", 2),
            baseline_test_descriptors=[test_descriptor.TestDescriptor("tests-x/a-test:::A.B")],
            conf=FAKE_CONF,
            baseline_conf=baseline_conf,
            env_vars=diff_env,
            args=args)

    baseline_job = next(job for job in jobs
                        if job['job_group'] == rts.FAILED_TESTS_ON_BASELINE_JOB_GROUP)
    assert baseline_job['env_vars']['YB_THIRDPARTY_DIR'] == '/opt/yb-build/thirdparty/baseline'
    # The URL travels with it, or a worker that does not have that directory cannot fetch it.
    assert baseline_job['env_vars']['YB_THIRDPARTY_URL'] == (
        'https://example.invalid/baseline-tp.tar.gz')
    # The toolchain stays the diff's: nothing on a worker fetches one, and llvm-symbolizer is
    # looked up under YB_THIRDPARTY_DIR, which now is the baseline's.
    assert baseline_job['env_vars']['YB_LLVM_TOOLCHAIN_DIR'] == '/opt/yb-build/llvm/diff'
    # The diff's re-runs are the ones that decide the build result; they keep their own tree.
    diff_job = next(job for job in jobs
                    if job['job_group'] == rts.FAILED_TESTS_ON_DIFF_JOB_GROUP)
    assert diff_job['env_vars']['YB_THIRDPARTY_DIR'] == '/opt/yb-build/thirdparty/diff'
    assert diff_job['env_vars']['YB_THIRDPARTY_URL'] == 'https://example.invalid/diff-tp.tar.gz'
    # The build log is the only place this is visible once the run ends - the per-test logs live in
    # the baseline tree, which the pipeline drops - so the line naming both is what a reader of a
    # finished build has to go on.
    assert ('Baseline test run: thirdparty /opt/yb-build/thirdparty/baseline, '
            "where the diff's is /opt/yb-build/thirdparty/diff") in caplog.text


def test_unrecorded_baseline_thirdparty_is_reported(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Any, caplog: Any) -> None:
    """
    Nothing can be done about it - a Spark worker is reused across tasks and
    set_env_on_spark_worker only assigns, so dropping the key leaves the previous task's value
    there.
    """
    baseline_root = tmp_path / 'baseline-build-root'
    baseline_root.mkdir()
    baseline_conf = yb_dist_tests.TestConfig(
        build_root=str(baseline_root),
        build_type=FAKE_CONF.build_type,
        yb_src_root=str(tmp_path),
        archive_for_workers=None,
        rel_build_root='build/baseline-build-root',
        archive_sha256sum=None,
        compiler_type=FAKE_CONF.compiler_type)
    diff_env = dict(FAKE_ENV)
    diff_env['YB_THIRDPARTY_DIR'] = '/opt/yb-build/thirdparty/diff'
    jobs: List[dict] = []

    def fake_run_tests_job(test_descriptors: List[test_descriptor.TestDescriptor], rerun: bool,
                           conf: Any, env_vars: Any, job_group: Any,
                           shares_context: bool,
                           stop_on_failure_limit: bool = True
                           ) -> List[yb_dist_tests.TestResult]:
        jobs.append({'job_group': job_group, 'env_vars': dict(env_vars)})
        return results_for(test_descriptors)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "BASELINE_SUBMIT_ORDER_DELAY_SEC", 0)
    monkeypatch.delenv("CSI_SERVER", raising=False)
    monkeypatch.delenv("CSI_TOKEN", raising=False)

    args = types.SimpleNamespace(
        baseline_csi_launch='launch-uuid', baseline_repetitions=3,
        baseline_status_file=str(tmp_path / 'baseline_status.txt'), baseline_test_list_file=None,
        baseline_commit_id=None)
    with caplog.at_level('WARNING'):
        rts.rerun_failed_tests_on_diff_and_baseline(
            diff_rerun_descriptors=make_attempts("tests-x/a-test:::A.B", 2),
            baseline_test_descriptors=[test_descriptor.TestDescriptor("tests-x/a-test:::A.B")],
            conf=FAKE_CONF,
            baseline_conf=baseline_conf,
            env_vars=diff_env,
            args=args)

    baseline_job = next(job for job in jobs
                        if job['job_group'] == rts.FAILED_TESTS_ON_BASELINE_JOB_GROUP)
    assert baseline_job['env_vars']['YB_THIRDPARTY_DIR'] == '/opt/yb-build/thirdparty/diff'
    assert 'recorded no YB_THIRDPARTY_DIR' in caplog.text


def test_a_stopped_context_is_detected_without_an_error(
        monkeypatch: pytest.MonkeyPatch, caplog: Any) -> None:
    """
    SparkContext.stop() clears _jsc, so asking the JVM whether the context is stopped raises
    AttributeError on the very context we just stopped - a known state, but the except branch logged
    it as an error with a traceback, which is misleading when triaging a real failure.
    """
    monkeypatch.setattr(rts, "spark_context", types.SimpleNamespace(_jsc=None))
    with caplog.at_level('ERROR'):
        assert rts.spark_context_is_stopped() is True
    assert caplog.records == []


def test_the_two_trees_archives_have_different_basenames() -> None:
    """
    Spark's addFile flattens everything it distributes into one directory per executor, keyed by
    basename, and the worker resolves its archive by basename out of that same directory. When both
    trees used the shared name, addFile refused the second archive outright - "File ... exists and
    does not match contents of ..." - and killed the run before any test ran. Equal basenames are
    what matters here, not equal paths: the two trees always live at different paths.
    """
    diff_conf = yb_dist_tests.make_conf_for_build_root(
        '/diff/yb-src-root/build/debug-clang17-dynamic-ninja', send_archive_to_workers=True,
        archive_name=yb_dist_tests.ARCHIVE_FOR_WORKERS_NAME)
    baseline_conf = yb_dist_tests.make_conf_for_build_root(
        '/baseline/yb-src-root/build/debug-clang17-dynamic-ninja', send_archive_to_workers=True,
        archive_name=yb_dist_tests.BASELINE_ARCHIVE_FOR_WORKERS_NAME)

    assert diff_conf.archive_for_workers is not None
    assert baseline_conf.archive_for_workers is not None
    assert (os.path.basename(diff_conf.archive_for_workers) !=
            os.path.basename(baseline_conf.archive_for_workers))
    # Both must keep the suffix that stops Spark unpacking them for us.
    for archive in (diff_conf.archive_for_workers, baseline_conf.archive_for_workers):
        assert archive.endswith('.tar.gz.spark-no-extract')


# ---------------------------------------------------------------------------------------------
# A re-created Spark context must carry every archive the old one had.
# ---------------------------------------------------------------------------------------------

def test_recreated_context_gets_both_trees_archives(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    addFile is per SparkContext, so a context re-created after a lost application starts with
    nothing distributed. The diff's archive comes back with the conf that re-creates it, but the
    baseline's was sent against the previous context - if it is not replayed, every baseline task on
    the new context fails with "Archive not found" and the repeat run the resubmit loop exists
    for degrades to failed-no-results. This is invisible without archives, which is why the local
    fault-injection run did not catch it.
    """
    sent: List[str] = []
    monkeypatch.setattr(rts, "g_spark_archives", [])
    monkeypatch.setattr(rts, "spark_context",
                        types.SimpleNamespace(addFile=lambda path: sent.append(path)))

    diff_archive = '/diff/build/flavor/archive.tar.gz.spark-no-extract'
    baseline_archive = '/baseline/build/flavor/archive_baseline.tar.gz.spark-no-extract'
    rts.remember_archive_for_workers(diff_archive)
    rts.remember_archive_for_workers(baseline_archive)

    # What init_spark_context() does on every creation, including a re-creation.
    rts.send_archives_to_workers()

    assert sent == [diff_archive, baseline_archive]

    # Remembering is idempotent: re-registering the diff's archive must not send it twice.
    rts.remember_archive_for_workers(diff_archive)
    assert len(rts.g_spark_archives) == 2


def test_baseline_cancellation_does_not_stop_the_diffs_resubmission(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """
    Both jobs run concurrently and both go through run_spark_action, so cancellation has to be
    tracked per job group. While it was one flag, a baseline that tripped its own failure threshold
    - what a broken base commit produces - made the loop stop re-submitting the *diff's* missing
    attempts, and main() then read the same flag as "expected" and reported success. Silent loss of
    the diff's re-runs, caused entirely by the failed-tests-on-baseline job.
    """
    attempts = make_attempts("tests-x/a-test:::A.B", 4)
    baseline_tests = [test_descriptor.TestDescriptor("tests-x/a-test:::A.B")]
    submissions: List[List[test_descriptor.TestDescriptor]] = []

    def fake_phase(diff_rerun_descriptors: List[test_descriptor.TestDescriptor],
                   baseline_test_descriptors: List[test_descriptor.TestDescriptor],
                   conf: Any, baseline_conf: Any, env_vars: Any,
                   args: Any) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(diff_rerun_descriptors))
        # The baseline hits its failure threshold and its job group is cancelled.
        rts.g_cancelled_job_groups.add(rts.FAILED_TESTS_ON_BASELINE_JOB_GROUP)
        # The diff's job loses the results of two attempts, which must be re-submitted.
        return results_for(diff_rerun_descriptors[:-2]) if len(submissions) == 1 \
            else results_for(diff_rerun_descriptors)

    monkeypatch.setattr(rts, "rerun_failed_tests_on_diff_and_baseline", fake_phase)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars={}, baseline_conf=FAKE_CONF,
        baseline_test_descriptors=baseline_tests, args=types.SimpleNamespace())

    assert len(submissions) == 2                       # the diff re-submitted despite the baseline
    assert len(submissions[1]) == 2                    # exactly the attempts that lost results
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)
    # The diff's own group was never cancelled.
    assert rts.FAILED_TESTS_ON_DIFF_JOB_GROUP not in rts.g_cancelled_job_groups


# -------------------------------------------------------------------------------------------------
# run_new_test_repetitions(): which tests get the extra runs.
#
# The selection decides what an integration lane spends extra test time on, and a wrong answer is
# not a wrong number but a flood: every test of the lane read as "new" would repeat the whole lane.
# So each guard that narrows the set is pinned here, with Spark and CSI faked out.
# -------------------------------------------------------------------------------------------------

def make_args(**overrides: Any) -> Any:
    """main()'s parsed arguments, as far as run_new_test_repetitions reads them."""
    args = dict(new_test_repetitions=20, num_repetitions=1, test_list=None, ignore_list=None,
                max_tests=None, test_filter_re=None, test_conf=None,
                known_test_list='/tmp/known_list.txt')
    args.update(overrides)
    return types.SimpleNamespace(**args)


def capture_submissions(monkeypatch: pytest.MonkeyPatch,
                        known: Any) -> List[Any]:
    """
    Fake out the known-test list the pipeline wrote and the Spark job, and record every submission
    as (descriptors, rerun, env_vars).
    """
    submissions: List[Any] = []

    def fake_run_tests_job(
            pending: List[test_descriptor.TestDescriptor],
            rerun: bool,
            conf: yb_dist_tests.TestConfig,
            env_vars: Any = None,
            **kwargs: Any) -> List[yb_dist_tests.TestResult]:
        # Its own Spark job group, so the failure monitor cancels only this job and its
        # cancellation cannot be mistaken for the main pass's.
        assert kwargs.get('job_group') == rts.NEW_TESTS_ON_DIFF_JOB_GROUP
        submissions.append((list(pending), rerun, env_vars))
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "load_known_test_list", lambda path: known)
    return submissions


def test_only_new_and_passing_tests_are_repeated(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    New is "the previous launch of this lane did not report it". Of those, only the ones that
    passed here are repeated: a new test that already failed is re-run by --fail_repetitions, and
    repeating only the passing ones is what makes retry_kind=new_test_repetition mean "the first
    attempt passed".
    """
    # What the previous launch of the lane reported, as the pipeline wrote it. The Gone ones are
    # tests it ran that this build no longer has, which is why its list can be longer than this
    # run's.
    known = {"tests-x/a-test:::A.Old", "tests-x/a-test:::A.OldFlaky",
             "tests-x/a-test:::A.Gone1", "tests-x/a-test:::A.Gone2"}
    submissions = capture_submissions(monkeypatch, known)
    results = [
        make_result("tests-x/a-test:::A.Old"),
        make_result("tests-x/a-test:::A.OldFlaky", exit_code=1),
        make_result("tests-x/a-test:::A.New"),
        make_result("tests-x/a-test:::A.NewBroken", exit_code=1),
    ]

    rts.run_new_test_repetitions(
        make_args(new_test_repetitions=20), FAKE_CONF, results, FAKE_ENV, False)

    assert len(submissions) == 1
    descriptors, rerun, env_vars = submissions[0]
    # 19 more runs of the one new test that passed, numbered from 2 so they do not collide with
    # the main pass's own attempt.
    assert descriptor_strs(descriptors) == {
        "tests-x/a-test:::A.New:::attempt_%d" % i for i in range(2, 21)
    }
    # Not a fail repetition: the kind those report would say this test's first attempt failed.
    assert rerun is False
    # The main pass's environment, which carries the CSI suite ids the workers report into, plus
    # the marker that makes these executions report as new-test repetitions.
    assert env_vars == dict(FAKE_ENV, YB_CSI_NEW_TEST='1')


def test_a_new_test_that_skipped_itself_is_not_repeated(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A test that skips itself (GTEST_SKIP, a platform gate) exits 0 and is reported to CSI as
    skipped. It is not a pass: repeating it would hang executions of nothing on a skipped item
    while the tag told readers its first attempt passed.
    """
    known = {"tests-x/a-test:::A.T%d" % i for i in range(20)}
    submissions = capture_submissions(monkeypatch, known)
    results = [make_result(uid) for uid in sorted(known)] + [
        make_result("tests-x/a-test:::A.NewSkipped", skipped=True),
        make_result("tests-x/a-test:::A.New")]

    rts.run_new_test_repetitions(make_args(new_test_repetitions=3), FAKE_CONF, results, FAKE_ENV,
                                 False)

    [(descriptors, _, _)] = submissions
    assert descriptor_strs(descriptors) == {
        "tests-x/a-test:::A.New:::attempt_2", "tests-x/a-test:::A.New:::attempt_3"}


def test_two_results_for_one_test_repeat_it_once(monkeypatch: pytest.MonkeyPatch) -> None:
    """A Spark task that reported and was then resubmitted leaves two results for one test."""
    known = {"tests-x/a-test:::A.T%d" % i for i in range(20)}
    submissions = capture_submissions(monkeypatch, known)
    results = [make_result(uid) for uid in sorted(known)] + [
        make_result("tests-x/a-test:::A.New"), make_result("tests-x/a-test:::A.New")]

    rts.run_new_test_repetitions(make_args(new_test_repetitions=3), FAKE_CONF, results, FAKE_ENV,
                                 False)

    [(descriptors, _, _)] = submissions
    assert len(descriptors) == 2


def test_nothing_is_repeated_without_a_known_test_list(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    No baseline, no notion of new: the first launch of a lane must not repeat all 12k tests. The
    pipeline writes no list when the lane has no comparable previous launch, and an older pipeline
    writes none at all, so the flag is simply absent.
    """
    submissions = capture_submissions(monkeypatch, {"tests-x/a-test:::A.Old"})

    rts.run_new_test_repetitions(
        make_args(known_test_list=None), FAKE_CONF, [make_result("tests-x/a-test:::A.New")],
        FAKE_ENV, False)

    assert submissions == []


def test_an_empty_known_test_list_repeats_nothing(monkeypatch: pytest.MonkeyPatch) -> None:
    """An empty file is not "no tests are known": it would make every test read as new."""
    submissions = capture_submissions(monkeypatch, set())

    rts.run_new_test_repetitions(
        make_args(), FAKE_CONF, [make_result("tests-x/a-test:::A.New")], FAKE_ENV, False)

    assert submissions == []


def test_load_known_test_list_reads_one_unique_id_per_line(tmp_path: Any) -> None:
    """The file is what csi/lib.groovy writes: uniqueIds one per line, possibly with blank lines."""
    path = tmp_path / "known_list.txt"
    path.write_text("tests-x/a-test:::A.Old\n\n  tests-x/a-test:::A.Other  \n"
                    "org.yb.SomeTest#testIt\n")

    assert rts.load_known_test_list(str(path)) == {
        "tests-x/a-test:::A.Old", "tests-x/a-test:::A.Other", "org.yb.SomeTest#testIt"}


def test_a_short_previous_launch_is_not_a_baseline(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    A launch that ended early reported a fraction of its tests, and every test it never reached
    would read as new. Below NEW_TEST_MIN_KNOWN_RATIO of this run's tests, it is not used.
    """
    results = [make_result("tests-x/a-test:::A.T%d" % i) for i in range(100)]
    known = {"tests-x/a-test:::A.T%d" % i for i in range(50)}
    submissions = capture_submissions(monkeypatch, known)

    rts.run_new_test_repetitions(make_args(), FAKE_CONF, results, FAKE_ENV, False)

    assert submissions == []


def test_too_many_new_tests_are_sampled(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    One source change can create hundreds of new test names (a parameterized instantiation, a
    renamed binary). Past NEW_TEST_LIMIT a sample of that size is repeated, not all of them.
    """
    results = [make_result("tests-x/a-test:::A.T%d" % i) for i in range(1200)]
    known = {"tests-x/a-test:::A.T%d" % i for i in range(1100)}
    submissions = capture_submissions(monkeypatch, known)

    rts.run_new_test_repetitions(
        make_args(new_test_repetitions=3), FAKE_CONF, results, FAKE_ENV, False)

    descriptors = submissions[0][0]
    repeated = set(td.descriptor_str_without_attempt_index for td in descriptors)
    assert len(repeated) == rts.NEW_TEST_LIMIT
    assert len(descriptors) == rts.NEW_TEST_LIMIT * 2
    assert repeated.isdisjoint(known)


@pytest.mark.parametrize('args', [
    make_args(new_test_repetitions=0),    # off
    make_args(new_test_repetitions=1),    # one run is what the main pass already did
    make_args(num_repetitions=5),         # every test is already being repeated
    make_args(test_list='/tmp/rerun_list.txt'),    # a re-run of named tests, not the lane's pass
    make_args(ignore_list='/tmp/ignore_list.txt'),
    make_args(max_tests=100),             # a sample of the lane, so "not run before" means nothing
    make_args(test_filter_re='A.*'),
    make_args(test_conf='/tmp/test_conf.json'),
])
def test_partial_runs_repeat_nothing(monkeypatch: pytest.MonkeyPatch, args: Any) -> None:
    """
    Only a full pass of the lane over its own test list can tell a new test from one this run was
    never going to execute. Every other shape of run repeats nothing, and reads no list.
    """
    def fail_if_called(path: str) -> Any:
        raise AssertionError("the known-test list must not be read for a partial run")

    submissions = capture_submissions(monkeypatch, {"tests-x/a-test:::A.Old"})
    monkeypatch.setattr(rts, "load_known_test_list", fail_if_called)

    rts.run_new_test_repetitions(
        args, FAKE_CONF, [make_result("tests-x/a-test:::A.New")], FAKE_ENV, False)

    assert submissions == []


def test_a_cancelled_main_pass_repeats_nothing(monkeypatch: pytest.MonkeyPatch) -> None:
    """
    The main pass was cancelled at the failure threshold, so this run's results are a partial
    picture of the lane and the tests it never reached are not evidence of anything.
    """
    submissions = capture_submissions(monkeypatch, {"tests-x/a-test:::A.Old"})

    rts.run_new_test_repetitions(
        make_args(), FAKE_CONF, [make_result("tests-x/a-test:::A.New")], FAKE_ENV, True)

    assert submissions == []
