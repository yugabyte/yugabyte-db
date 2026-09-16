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
Unit tests for the Spark job submission orchestration in run_tests_on_spark.py, specifically
run_tests_job_with_resubmits(): it re-submits the Spark job (both the initial test job and the
failed-test re-run job) for test attempts that did not produce a result (e.g. because the Spark
application was lost while autoscaled workers were shutting down), re-creating the Spark context
when it was stopped.

These tests mock out Spark: run_tests_job(), spark_context_is_stopped() and restart_spark_context()
are patched, so no Spark cluster (or pyspark) is exercised. Only the driver-side orchestration
logic is under test.
"""

import types
from typing import Any, List, Set

import pytest

# Import the module (not the TestDescriptor class) so pytest does not try to collect the
# Test*-named class as a test case.
from yugabyte import run_tests_on_spark as rts
from yugabyte import test_descriptor
from yugabyte import yb_dist_tests


# run_tests_job() is faked in every test here, so the conf is only threaded through, never used.
# None of these paths are touched.
FAKE_CONF = yb_dist_tests.TestConfig(
    build_root='/fake/yb-src-root/build/debug-clang17-dynamic-ninja',
    build_type='debug',
    yb_src_root='/fake/yb-src-root',
    archive_for_workers=None,
    rel_build_root='build/debug-clang17-dynamic-ninja',
    archive_sha256sum=None,
    compiler_type='clang17')

# The per-task environment the wrapper must hand to every submission. A distinctive value so a
# test can assert it arrived rather than merely that something was passed: the wrapper reaching for
# the module-level propagated_env_vars instead of its parameter is exactly the bug this catches.
FAKE_ENV = {'YB_FAKE_ENV_MARKER': 'from-the-caller'}


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
    Neutralize the real Spark-touching helpers and reset the module-global cancellation flag
    before each test.
    """
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setattr(rts, "restart_spark_context", lambda conf: None)
    monkeypatch.setattr(rts, "g_spark_job_cancelled", False)
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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

    assert len(submissions) == 1
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # First submission loses the second half; the resubmission returns everything it is given.
        returned = list(pending)[:5] if len(submissions) == 1 else list(pending)
        return results_for(returned)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
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
    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)
    assert len(submissions) == 3
    assert restart_count == 3

    # Context reports alive -> never restart, even though resubmissions still happen.
    submissions.clear()
    restart_count = 0
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)
    rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)
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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        events.append("submit")
        return results_for(pending)

    def record_restart(conf: yb_dist_tests.TestConfig) -> None:
        events.append("restart")
        stopped["value"] = False

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "restart_spark_context", record_restart)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: stopped["value"])

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        if submissions["n"] == 1:
            raise Py4JJavaError("Cannot call methods on a stopped SparkContext.")
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        return results_for(pending)

    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)
    monkeypatch.setattr(rts, "restart_spark_context", flaky_restart)
    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions["n"] += 1
        if submissions["n"] == 1:
            raise EOFError("gateway connection closed")
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: True)  # application is down

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        raise ValueError("unrelated bug")

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)
    monkeypatch.setattr(rts, "spark_context_is_stopped", lambda: False)  # context is fine

    with pytest.raises(ValueError, match="unrelated bug"):
        rts.run_tests_job_with_resubmits(attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)


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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        # Simulate run_spark_action detecting the deliberate cancellation.
        rts.g_spark_job_cancelled = True
        return results_for(list(pending)[:3])

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions.append(list(pending))
        return []

    monkeypatch.setattr(rts, "SPARK_JOB_MAX_SUBMITS", 5)
    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
        submissions.append([td.descriptor_str for td in pending])
        # Lose exactly one attempt of each test on the first pass.
        if len(submissions) == 1:
            return results_for([td for td in pending if td.attempt_index != 2])
        return results_for(pending)

    monkeypatch.setattr(rts, "run_tests_job", fake_run_tests_job)

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
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

    results = rts.run_tests_job_with_resubmits(
        attempts, rerun=True, conf=FAKE_CONF, env_vars=FAKE_ENV)

    assert len(submissions) == 2       # first submission + one recovery submission
    assert len(submissions[1]) == 3    # exactly the 3 dropped attempts are re-submitted
    assert restart_count == 1          # context re-created once before the recovery submission
    assert descriptor_strs([r.test_descriptor for r in results]) == descriptor_strs(attempts)


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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
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
            env_vars: Any = None) -> List[yb_dist_tests.TestResult]:
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


def test_no_function_shadows_a_module_level_import() -> None:
    """
    A function-level "from yugabyte import yb_dist_tests" makes that name local for the *whole*
    function body, so any use of it earlier in the function raises UnboundLocalError. That is how
    parallel_run_test() failed on every Spark task once this refactoring had it parse the conf
    before that import. Python decides local-vs-global at compile time, so the mocked tests above
    cannot catch it - only running a real task would. This check does, statically.
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
