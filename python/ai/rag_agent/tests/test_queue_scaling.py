#!/usr/bin/env python3
"""
Tests for the work queue behaviors that keep document indexing reliable at scale:
source tasks before document tasks, a not-before time for retries, re-queuing
tasks whose worker died, a sweep for documents left with no task, an atomic
document claim, retrying a rate-limited document instead of failing it, and
renewing the lease of a task that is still running.
"""

import threading
import time
from unittest.mock import Mock, patch
from uuid import uuid4

from work_queue.poller import Poller


def _poller(mock_pool_cls):
    mock_pool = Mock()
    mock_pool_cls.return_value = mock_pool
    conn, cur = Mock(), Mock()
    mock_pool.get_connection.return_value = conn
    conn.cursor.return_value = cur
    poller = Poller()
    # Keep the periodic reap and sweep out of tests that don't exercise them.
    poller._last_reap = poller._last_sweep = time.monotonic()
    return poller, conn, cur


def _claims(cur):
    return [c.args for c in cur.execute.call_args_list if "UPDATE dist_rag.work_queue" in c.args[0]]


@patch("work_queue.poller.ConnectionPool")
class TestPollOrder:
    def test_a_source_task_is_claimed_before_any_document_task(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.return_value = ("row",)
        with patch("work_queue.poller.WorkQueueTask.from_db_row", return_value="task"):
            assert poller.poll("worker", ["text/plain"]) == "task"
        claims = _claims(cur)
        assert len(claims) == 1
        assert "task_type = 'CREATE_SOURCE'" in claims[0][0]

    def test_the_source_claim_pins_the_null_document_type(self, mock_pool_cls):
        """Without it the claim reads past every PREPROCESS row in the index."""
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.return_value = None
        poller.poll("worker", ["text/plain"])
        assert "task_details->>'document_type' IS NULL" in _claims(cur)[0][0]

    def test_with_no_source_task_the_oldest_matching_document_task_is_claimed(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.side_effect = [None, ("row",)]
        with patch("work_queue.poller.WorkQueueTask.from_db_row", return_value="task"):
            assert poller.poll("worker", ["text/plain"]) == "task"
        sql, params = _claims(cur)[1]
        assert "task_type = 'PREPROCESS'" in sql
        assert "text/plain" in params

    def test_a_task_released_for_retry_is_not_claimed_before_its_time(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.return_value = None
        assert poller.poll("worker", ["text/plain"]) is None
        assert all("created_at <= CURRENT_TIMESTAMP" in sql for sql, _ in _claims(cur))


@patch("work_queue.poller.ConnectionPool")
class TestRecovery:
    def test_expired_leases_are_requeued_at_most_once_a_minute(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        poller._last_reap = 0.0
        cur.rowcount = 2
        poller._maybe_reap_expired(cur)
        poller._maybe_reap_expired(cur)
        reaps = [c for c in cur.execute.call_args_list
                 if "lease_expires_at < CURRENT_TIMESTAMP" in c.args[0]]
        assert len(reaps) == 1
        assert "task_status = 'QUEUED'" in reaps[0].args[0]

    def test_release_puts_a_task_back_with_a_delay(self, mock_pool_cls):
        poller, _, _ = _poller(mock_pool_cls)
        task_id = uuid4()
        with patch.object(poller, "_execute_dml", return_value=True) as dml:
            assert poller.release(task_id, 60)
        kwargs = dml.call_args.kwargs
        assert "created_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s" in kwargs["sql"]
        assert kwargs["params"] == (60, str(task_id))
        assert "'{attempt}'" in kwargs["sql"], "each release counts an attempt"

    def test_finishing_a_task_is_fenced_on_its_lease(self, mock_pool_cls):
        poller, _, _ = _poller(mock_pool_cls)
        task_id, lease = uuid4(), uuid4()
        for finish in (poller.mark_completed, poller.mark_failed):
            with patch.object(poller, "_execute_dml", return_value=True) as dml:
                finish(task_id, lease_token=lease)
            kwargs = dml.call_args.kwargs
            assert "lease_token = %s" in kwargs["sql"]
            assert kwargs["params"][-2:] == (str(task_id), str(lease))
        with patch.object(poller, "_execute_dml", return_value=True) as dml:
            poller.release(task_id, 60, lease_token=lease)
        assert dml.call_args.kwargs["params"] == (60, str(task_id), str(lease))

    def test_the_sweep_resets_stuck_documents_and_queues_each_source_once(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        poller._last_sweep = 0.0
        stuck, queued, source = uuid4(), uuid4(), uuid4()
        cur.fetchall.return_value = [(stuck, source, "PROCESSING"), (queued, source, "QUEUED")]
        poller._maybe_sweep_orphans(cur)
        sqls = [c.args for c in cur.execute.call_args_list]
        resets = [a for a in sqls
                  if a[0].startswith("UPDATE dist_rag.documents SET status = 'QUEUED'")]
        assert resets and resets[0][1] == ([str(stuck)],)
        queues = [a for a in sqls if "_queue_source_documents" in a[0]]
        assert len(queues) == 1 and queues[0][1] == (str(source), str(source))

    def test_the_sweep_does_nothing_without_orphans(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        poller._last_sweep = 0.0
        cur.fetchall.return_value = []
        poller._maybe_sweep_orphans(cur)
        assert len(cur.execute.call_args_list) == 1


class TestClaimAndRetry:
    def test_a_document_held_by_another_task_is_not_claimed(self):
        from db.source_document_tracking import SourceDocumentTracking

        tracking = SourceDocumentTracking.__new__(SourceDocumentTracking)
        conn, cur = Mock(), Mock()
        tracking.connection_pool = Mock(get_connection=Mock(return_value=conn))
        conn.cursor.return_value = cur
        cur.fetchone.return_value = None
        assert tracking.claim_document(uuid4(), task_id=uuid4()) is False
        sql = cur.execute.call_args.args[0]
        assert "status NOT IN ('PROCESSING', 'COMPLETED')" in sql
        assert "claimed_by" in sql

    def test_a_document_already_listed_is_skipped_not_failed(self):
        from db.source_document_tracking import SourceDocumentTracking

        tracking = SourceDocumentTracking.__new__(SourceDocumentTracking)
        tracking.logger = Mock()
        conn, cur = Mock(), Mock()
        tracking.connection_pool = Mock(get_connection=Mock(return_value=conn))
        conn.cursor.return_value = cur
        cur.fetchone.return_value = None
        metadata = Mock(source_id=uuid4(), document_uri="s3://b/a.txt")
        assert tracking.insert_document_metadata(metadata) is None
        assert "ON CONFLICT DO NOTHING" in cur.execute.call_args.args[0]
        conn.commit.assert_called_once()

    def test_rate_limits_are_recognized(self):
        from rag_pipeline.document_preprocessor import _is_rate_limited

        class _Http(Exception):
            status_code = 429

        assert _is_rate_limited(_Http())
        assert _is_rate_limited(Exception("Error code: 429 - Too many requests"))
        assert _is_rate_limited(type("ThrottlingException", (Exception,), {})("slow down"))
        assert not _is_rate_limited(ValueError("bad document"))
        assert not _is_rate_limited(ValueError("cannot parse s3://bucket/report-4291.pdf"))
        assert not _is_rate_limited(RuntimeError("request id 7f429a1c failed"))

    def test_a_wrapped_rate_limit_is_recognized(self):
        from rag_pipeline.document_preprocessor import _is_rate_limited

        class _Http(Exception):
            status_code = 429

        try:
            try:
                raise _Http()
            except _Http as inner:
                raise RuntimeError("embedding failed") from inner
        except RuntimeError as outer:
            assert _is_rate_limited(outer)


class _Renewals:
    """A renew_lease stand-in that records calls and lets a test wait for the Nth."""

    def __init__(self, results):
        self.results = list(results)
        self.calls = 0
        self.args = None
        self._cond = threading.Condition()

    def __call__(self, *args):
        with self._cond:
            self.calls += 1
            self.args = args
            self._cond.notify_all()
            result = self.results[min(self.calls, len(self.results)) - 1]
        if isinstance(result, Exception):
            raise result
        return result

    def wait_for(self, n, timeout=15):
        with self._cond:
            return self._cond.wait_for(lambda: self.calls >= n, timeout)


@patch("work_queue.poller.ConnectionPool")
class TestKeepLease:
    def _task(self):
        return Mock(id=uuid4(), lease_token=uuid4())

    def test_lease_is_renewed_every_third_of_its_duration_until_block_ends(self, mock_pool_cls):
        poller, _, _ = _poller(mock_pool_cls)
        task = self._task()
        renew = _Renewals([("expires",)])
        with patch.object(poller, "renew_lease", side_effect=renew):
            with poller.keep_lease(task, lease_duration_seconds=3):
                assert renew.wait_for(3), "the lease should be renewed while the block runs"
            calls = renew.calls
            time.sleep(1.5)
        assert renew.calls == calls, "renewal stopped when the block ended"
        assert renew.args == (str(task.id), str(task.lease_token), 3)

    def test_renewal_stops_once_the_lease_is_lost(self, mock_pool_cls):
        poller, _, _ = _poller(mock_pool_cls)
        renew = _Renewals([None])
        with patch.object(poller, "renew_lease", side_effect=renew):
            with poller.keep_lease(self._task(), lease_duration_seconds=3):
                assert renew.wait_for(1)
                time.sleep(1.5)
        assert renew.calls == 1

    def test_a_failed_renewal_is_retried(self, mock_pool_cls):
        poller, _, _ = _poller(mock_pool_cls)
        renew = _Renewals([RuntimeError("db"), ("expires",)])
        with patch.object(poller, "renew_lease", side_effect=renew):
            with poller.keep_lease(self._task(), lease_duration_seconds=3):
                assert renew.wait_for(2), "a renewal that raised should be retried"


class TestRetryIsIdempotent:
    @patch("db.yugabytedb_vector_store.PipelineTracking")
    @patch("db.yugabytedb_vector_store.ConnectionPool")
    def _insert(self, document_id, rows, mock_pool_cls, _, lease_check=None):
        from db.yugabytedb_vector_store import YugabyteDBVectorStore
        store = YugabyteDBVectorStore()
        conn = Mock()
        store.pool.get_connection.return_value = conn
        cur = conn.cursor.return_value
        cur.rowcount = 2
        cur.batches = []
        cur.executemany.side_effect = lambda sql, rows: cur.batches.append((sql, list(rows)))
        store._ensure_table_exists = Mock(return_value=True)
        store.insert_embeddings(document_id, "kb", iter(rows), pipeline_id=uuid4(),
                                batch_size=2, lease_check=lease_check)
        return conn, cur

    def test_a_rerun_writes_the_same_chunk_ids_and_skips_existing_ones(self):
        doc = uuid4()
        rows = [("chunk", [0.1]) for _ in range(3)]
        ids = []
        for _ in range(2):
            _, cur = self._insert(doc, rows)
            assert "ON CONFLICT DO NOTHING" in str(cur.batches[0][0])
            ids.append([row[0] for _, rows in cur.batches for row in rows])
        assert ids[0] == ids[1] and len(set(ids[0])) == 3

    def test_a_worker_that_lost_its_lease_commits_no_more_chunks(self):
        from db.yugabytedb_vector_store import LeaseLostError
        rows = [("chunk", [0.1]) for _ in range(4)]
        try:
            self._insert(uuid4(), rows, lease_check=lambda: False)
            raise AssertionError("expected LeaseLostError")
        except LeaseLostError:
            pass


class TestPollByDocumentType:
    @patch("work_queue.poller.ConnectionPool")
    def test_each_document_type_is_claimed_with_its_own_indexed_read(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.return_value = None
        poller.poll("worker", ["text/plain", "application/pdf"])
        claims = _claims(cur)
        assert len(claims) == 3
        assert all(" IN (" not in sql for sql, _ in claims)
        assert [params[-1] for _, params in claims[1:]] == ["text/plain", "application/pdf"]

    @patch("work_queue.poller.ConnectionPool")
    def test_the_first_type_tried_rotates_between_polls(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        cur.fetchone.return_value = None
        poller.poll("worker", ["a", "b"])
        poller.poll("worker", ["a", "b"])
        firsts = [params[-1] for sql, params in _claims(cur) if "PREPROCESS" in sql][::2]
        assert firsts == ["a", "b"]

    @patch("work_queue.poller.ConnectionPool")
    def test_a_failed_reap_is_retried_on_the_next_poll(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        poller._last_reap = 0.0
        cur.execute.side_effect = RuntimeError("db")
        try:
            poller._maybe_reap_expired(cur)
        except RuntimeError:
            pass
        assert poller._last_reap == 0.0


class TestCreateSource:
    def _processor(self):
        from rag_pipeline.aws_source_processor import CreateSourceProcessorForAWS_S3
        proc = CreateSourceProcessorForAWS_S3.__new__(CreateSourceProcessorForAWS_S3)
        proc.logger = Mock()
        proc.s3_crawler = Mock()
        proc.poller = Mock()
        proc.connection_pool = Mock()
        proc.source_document_tracking = Mock()
        proc._update_create_source_status = Mock()
        proc._get_documents_from_source = Mock()
        return proc

    def _task(self, attempt=0):
        details = {"source_id": str(uuid4())}
        if attempt:
            details["attempt"] = attempt
        return Mock(id=uuid4(), lease_token=uuid4(), task_details=details)

    def test_a_missing_source_fails_the_task_instead_of_looping(self):
        proc = self._processor()
        proc.source_document_tracking.get_source_details.return_value = None
        task = self._task()
        proc.process(task)
        proc.poller.mark_failed.assert_called_once_with(task.id, lease_token=task.lease_token)

    def test_a_worker_without_an_s3_crawler_fails_the_task(self):
        proc = self._processor()
        proc.s3_crawler = None
        task = self._task()
        try:
            proc.process(task)
            raise AssertionError("expected the error to propagate")
        except RuntimeError:
            pass
        proc.poller.mark_failed.assert_called_once_with(task.id, lease_token=task.lease_token)

    def test_a_failed_completion_is_retried_not_failed(self):
        proc = self._processor()
        proc._mark_task_completed = Mock(side_effect=RuntimeError("40001 conflict"))
        task = self._task()
        assert proc.process(task)["status"] == "retry"
        proc.poller.release.assert_called_once()
        proc.poller.mark_failed.assert_not_called()

    def test_a_completion_that_keeps_failing_eventually_fails_the_task(self):
        proc = self._processor()
        proc._mark_task_completed = Mock(side_effect=RuntimeError("bad"))
        task = self._task(attempt=proc.COMPLETION_MAX_RETRIES)
        try:
            proc.process(task)
            raise AssertionError("expected the error to propagate")
        except RuntimeError:
            pass
        proc.poller.mark_failed.assert_called_once()

    def test_a_lost_lease_writes_nothing(self):
        proc = self._processor()
        proc._mark_task_completed = Mock(return_value=False)
        assert proc.process(self._task())["status"] == "lease_lost"
        proc.poller.mark_failed.assert_not_called()
        proc._update_create_source_status.assert_called_once()  # IN_PROGRESS only

    def test_queuing_runs_after_the_completion_commits_and_its_failure_is_not_fatal(self):
        proc = self._processor()
        conn = proc.connection_pool.get_connection.return_value
        conn.cursor.return_value.rowcount = 1
        proc._queue_source_documents = Mock()
        assert proc._mark_task_completed(uuid4(), uuid4(), lease_token=uuid4())
        assert conn.commit.called
        proc._queue_source_documents.assert_called_once()

        failing = self._processor()
        failing.connection_pool.get_connection.return_value.cursor.return_value.execute \
            .side_effect = RuntimeError("txn too large")
        failing._queue_source_documents(uuid4())  # logs; the sweep recovers


@patch("work_queue.poller.ConnectionPool")
class TestMaintenanceNeverBlocksClaims:
    def test_a_failing_sweep_still_lets_the_worker_claim(self, mock_pool_cls):
        poller, conn, cur = _poller(mock_pool_cls)
        poller._last_sweep = 0.0
        claimed = ("row",)

        def execute(sql, params=None):
            if "FROM dist_rag.documents d" in sql:
                raise RuntimeError("sweep failed")

        cur.execute.side_effect = execute
        cur.fetchone.return_value = claimed
        with patch("work_queue.poller.WorkQueueTask.from_db_row", return_value="task"):
            assert poller.poll("worker", ["text/plain"]) == "task"
        conn.rollback.assert_called()
        assert poller._last_sweep == 0.0, "the failed sweep is retried on the next poll"

    def test_the_sweep_only_considers_sources_mapped_to_an_index(self, mock_pool_cls):
        poller, _, cur = _poller(mock_pool_cls)
        poller._last_sweep = 0.0
        cur.fetchall.return_value = []
        poller._maybe_sweep_orphans(cur)
        assert "vector_index_source_mappings" in cur.execute.call_args_list[0].args[0]


class TestChunkIds:
    def test_same_content_gives_the_same_id_and_changed_content_a_new_one(self):
        from db.yugabytedb_vector_store import chunk_id
        doc = uuid4()
        assert chunk_id(doc, 1, "a") == chunk_id(doc, 1, "a")
        assert chunk_id(doc, 1, "a") != chunk_id(doc, 1, "b")
        assert chunk_id(doc, 1, "a") != chunk_id(doc, 2, "a")
