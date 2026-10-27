#!/usr/bin/env python3
"""
Work queue behaviors from test_queue_scaling.py, run against a real database
with pg_dist_rag installed. Skipped unless RAG_TEST_DATABASE_URL is set; the
tests delete every row in the dist_rag tables of that database.

    RAG_TEST_DATABASE_URL=postgresql://yugabyte@localhost:15433/agentdb \
        pytest tests/test_queue_db.py
"""

import os
import threading
import time
from uuid import uuid4

import psycopg
import pytest

from db.connection_pool import ConnectionPool
from db.source_document_tracking import SourceDocumentTracking
from work_queue.poller import Poller

DB_URL = os.environ.get("RAG_TEST_DATABASE_URL")
pytestmark = [
    pytest.mark.requires_db,
    pytest.mark.skipif(not DB_URL, reason="RAG_TEST_DATABASE_URL is not set"),
]

TEXT = ["text/plain"]


@pytest.fixture(scope="module", autouse=True)
def pool():
    ConnectionPool.initialize(DB_URL, min_size=1, max_size=20)
    yield
    # The pool is a process-wide singleton; leave none behind for other modules.
    ConnectionPool.close_all()


@pytest.fixture
def db():
    conn = psycopg.connect(DB_URL, autocommit=True)
    # Children before parents: pipeline_details references documents.
    for table in ("work_queue", "pipeline_details", "documents",
                  "vector_index_source_mappings", "vector_indexes", "sources"):
        conn.execute(f"DELETE FROM dist_rag.{table}")
    yield conn
    conn.close()


def _poller():
    poller = Poller()
    # Reap and sweep only where a test calls them.
    poller._last_reap = poller._last_sweep = time.monotonic()
    return poller


def _index(db, source):
    # index_name is unique; the column default would collide on a second index.
    index = db.execute(
        "INSERT INTO dist_rag.vector_indexes "
        "(index_name, schema_name, index_creation_status, ai_provider) "
        "VALUES (%s, 'public', 'INIT', 'AWS_BEDROCK') RETURNING id",
        (f"idx_{uuid4().hex[:12]}",),
    ).fetchone()[0]
    db.execute(
        "INSERT INTO dist_rag.vector_index_source_mappings (index_id, source_id) VALUES (%s, %s)",
        (index, source),
    )
    return index


def _source(db, status="COMPLETED", completed_ago="10 minutes"):
    source = db.execute(
        "INSERT INTO dist_rag.sources (source_uri, status, completed_at) "
        "VALUES ('s3://bucket/t/', %s, now() - %s::interval) RETURNING id",
        (status, completed_ago),
    ).fetchone()[0]
    _index(db, source)
    return source


def _index_of(db, source):
    return db.execute(
        "SELECT index_id FROM dist_rag.vector_index_source_mappings WHERE source_id = %s "
        "ORDER BY id LIMIT 1",
        (source,),
    ).fetchone()[0]


def _document(db, source, status="QUEUED", uri=None):
    return db.execute(
        "INSERT INTO dist_rag.documents "
        "(source_id, document_name, document_uri, status, document_type) "
        "VALUES (%s, 'a.txt', %s, %s, 'text/plain') RETURNING document_id",
        (source, uri or f"s3://bucket/t/{uuid4().hex}.txt", status),
    ).fetchone()[0]


def _task(db, task_type="PREPROCESS", document=None, ago="0 seconds"):
    # dist_rag.create_source writes no document_type for CREATE_SOURCE tasks.
    details = {"document_type": "text/plain"} if task_type == "PREPROCESS" else {}
    if document:
        details["document_id"] = str(document)
        # Queued tasks always carry their index; the per-index guards match on it.
        source = db.execute(
            "SELECT source_id FROM dist_rag.documents WHERE document_id = %s", (document,)
        ).fetchone()[0]
        details["index_id"] = str(_index_of(db, source))
    return db.execute(
        "INSERT INTO dist_rag.work_queue (task_type, task_details, created_at) "
        "VALUES (%s, %s, now() - %s::interval) RETURNING id",
        (task_type, psycopg.types.json.Jsonb(details), ago),
    ).fetchone()[0]


def _run(fn):
    """Run a poller maintenance step on a pooled connection, as poll() does."""
    conn = ConnectionPool.get_connection()
    try:
        fn(conn.cursor())
        conn.commit()
    finally:
        ConnectionPool.return_connection(conn)


def _preprocess_tasks(db, document):
    return db.execute(
        "SELECT count(*) FROM dist_rag.work_queue WHERE task_type = 'PREPROCESS' "
        "AND task_details->>'document_id' = %s",
        (str(document),),
    ).fetchone()[0]


class TestClaimOrder:
    def test_a_new_source_task_is_claimed_ahead_of_an_older_document_backlog(self, db):
        for _ in range(50):
            _task(db, ago="1 hour")
        source_task = _task(db, task_type="CREATE_SOURCE")
        task = _poller().poll(str(uuid4()), TEXT)
        assert task.id == source_task

    def test_concurrent_workers_claim_each_task_exactly_once(self, db):
        tasks = {_task(db, ago=f"{i} seconds") for i in range(100)}
        claimed, lock = [], threading.Lock()

        def worker():
            poller = _poller()
            while True:
                task = poller.poll(str(uuid4()), TEXT)
                if task is None:
                    return
                with lock:
                    claimed.append(task.id)

        threads = [threading.Thread(target=worker) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert sorted(map(str, claimed)) == sorted(map(str, tasks))

    def test_a_released_task_waits_out_its_delay(self, db):
        task_id = _task(db)
        poller = _poller()
        assert poller.poll(str(uuid4()), TEXT).id == task_id
        assert poller.release(task_id, 60)
        assert poller.poll(str(uuid4()), TEXT) is None
        db.execute("UPDATE dist_rag.work_queue SET created_at = now() - interval '1 second'")
        assert poller.poll(str(uuid4()), TEXT).id == task_id


class TestLeases:
    def test_an_expired_lease_is_requeued_and_redelivered_under_the_same_id(self, db):
        task_id = _task(db)
        poller = _poller()
        poller.poll(str(uuid4()), TEXT, lease_duration_seconds=1)
        time.sleep(1.5)
        poller._last_reap = 0.0
        _run(poller._maybe_reap_expired)
        row = db.execute(
            "SELECT task_status, lease_token FROM dist_rag.work_queue WHERE id = %s", (task_id,)
        ).fetchone()
        assert row == ("QUEUED", None)
        assert poller.poll(str(uuid4()), TEXT).id == task_id

    def test_a_live_worker_keeps_its_task_past_its_lease(self, db):
        """keep_lease renews the lease, so the reaper leaves a running task alone."""
        task_id = _task(db, task_type="CREATE_SOURCE")
        poller = _poller()
        task = poller.poll(str(uuid4()), TEXT, lease_duration_seconds=3)
        with poller.keep_lease(task, lease_duration_seconds=3):
            time.sleep(4.5)
            poller._last_reap = 0.0
            _run(poller._maybe_reap_expired)
            row = db.execute(
                "SELECT task_status, lease_token FROM dist_rag.work_queue WHERE id = %s", (task_id,)
            ).fetchone()
        assert row == ("IN_PROGRESS", task.lease_token)


def _lease(db, task_id, seconds=600):
    """Put a task IN_PROGRESS under a new lease, as poll() does; return the lease token."""
    return db.execute(
        "UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS', "
        "lease_token = gen_random_uuid(), "
        "lease_expires_at = now() + %s * interval '1 second' "
        "WHERE id = %s RETURNING lease_token",
        (seconds, task_id),
    ).fetchone()[0]


class TestDocumentClaim:
    def test_only_one_of_two_live_tasks_claims_a_document(self, db):
        doc = _document(db, _source(db))
        tracking = SourceDocumentTracking()
        first, second = _task(db, document=doc), _task(db, document=doc)
        first_lease, second_lease = _lease(db, first), _lease(db, second)
        assert tracking.claim_document(doc, task_id=first, lease_token=first_lease)
        assert not tracking.claim_document(doc, task_id=second, lease_token=second_lease)

    def test_a_redelivered_task_reclaims_its_document_and_fences_the_old_worker(self, db):
        doc = _document(db, _source(db))
        tracking = SourceDocumentTracking()
        task_id = _task(db, document=doc)
        old_lease = _lease(db, task_id)
        assert tracking.claim_document(doc, task_id=task_id, lease_token=old_lease)
        new_lease = _lease(db, task_id)  # the reaper re-queued it; another worker polled it
        assert tracking.claim_document(doc, task_id=task_id, lease_token=new_lease)
        poller = _poller()
        assert not poller.mark_completed(task_id, lease_token=old_lease)
        assert not poller.release(task_id, 60, lease_token=old_lease)
        assert poller.mark_completed(task_id, lease_token=new_lease)

    def test_a_document_whose_lease_expired_can_be_claimed_by_another_task(self, db):
        doc = _document(db, _source(db))
        tracking = SourceDocumentTracking()
        first, second = _task(db, document=doc), _task(db, document=doc)
        first_lease = _lease(db, first, seconds=-1)
        assert tracking.claim_document(doc, task_id=first, lease_token=first_lease)
        assert tracking.claim_document(doc, task_id=second, lease_token=_lease(db, second))

    def test_concurrent_claims_have_one_winner(self, db):
        doc = _document(db, _source(db))
        results, lock = [], threading.Lock()

        def claim():
            won = SourceDocumentTracking().claim_document(doc, task_id=uuid4())
            with lock:
                results.append(won)

        threads = [threading.Thread(target=claim) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert results.count(True) == 1

    def test_a_completed_document_is_not_claimed(self, db):
        doc = _document(db, _source(db), status="COMPLETED")
        assert not SourceDocumentTracking().claim_document(doc, task_id=uuid4())


class TestQueueSourceDocuments:
    def _queue(self, db, source):
        db.execute(
            "SELECT dist_rag._queue_source_documents(%s, m.index_id) "
            "FROM dist_rag.vector_index_source_mappings m WHERE m.source_id = %s",
            (source, source),
        )

    def test_queuing_a_source_twice_gives_each_document_one_task(self, db):
        source = _source(db)
        docs = [_document(db, source) for _ in range(3)]
        self._queue(db, source)
        self._queue(db, source)
        assert [_preprocess_tasks(db, d) for d in docs] == [1, 1, 1]

    def test_a_document_whose_task_is_running_is_not_queued_again(self, db):
        source = _source(db)
        doc = _document(db, source)
        task_id = _task(db, document=doc)
        db.execute("UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS' WHERE id = %s",
                   (task_id,))
        self._queue(db, source)
        assert _preprocess_tasks(db, doc) == 1

    def test_each_index_sharing_a_source_gets_its_own_task(self, db):
        source = _source(db)
        doc = _document(db, source)
        _index(db, source)
        self._queue(db, source)
        self._queue(db, source)
        assert _preprocess_tasks(db, doc) == 2

    def test_a_failed_document_with_no_task_is_queued_for_retry(self, db):
        source = _source(db)
        doc = _document(db, source, status="FAILED")
        self._queue(db, source)
        assert _preprocess_tasks(db, doc) == 1


class TestSweep:
    def _sweep(self):
        poller = _poller()
        poller._last_sweep = 0.0
        _run(poller._maybe_sweep_orphans)

    def test_a_document_stuck_processing_with_no_task_is_requeued(self, db):
        doc = _document(db, _source(db), status="PROCESSING")
        self._sweep()
        status = db.execute(
            "SELECT status FROM dist_rag.documents WHERE document_id = %s", (doc,)
        ).fetchone()[0]
        assert status == "QUEUED"
        assert _preprocess_tasks(db, doc) == 1

    def test_a_document_with_a_live_task_is_left_alone(self, db):
        doc = _document(db, _source(db))
        _task(db, document=doc)
        self._sweep()
        assert _preprocess_tasks(db, doc) == 1

    def test_a_recently_completed_source_is_not_swept(self, db):
        doc = _document(db, _source(db, completed_ago="1 minute"))
        self._sweep()
        assert _preprocess_tasks(db, doc) == 0
