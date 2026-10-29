#!/usr/bin/env python3
"""
Work queue behaviors from test_queue_scaling.py, run against a real database
with pg_dist_rag 0.0.4 installed. Skipped unless RAG_TEST_DATABASE_URL is set;
the tests delete every row in the dist_rag tables of that database.

    RAG_TEST_DATABASE_URL=postgresql://yugabyte@localhost:15433/agentdb \
        pytest tests/test_queue_db.py
"""

import os
import threading
import time
from uuid import uuid4

import psycopg
import pytest

from db.capabilities import Capabilities
from db.connection_pool import ConnectionPool
from db.source_document_tracking import SourceDocumentTracking
from models.document_metadata import DocumentMetadata
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
    conn.execute("UPDATE dist_rag.maintenance_runs SET last_run_at = 'epoch'")
    Capabilities.reset()
    yield conn
    conn.close()


def _poller():
    poller = Poller()
    # Sweep only where a test calls it.
    poller._last_sweep = time.monotonic()
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


def _source(db, status="COMPLETED", completed_ago="10 minutes", mapped=True):
    source = db.execute(
        "INSERT INTO dist_rag.sources (source_uri, status, completed_at) "
        "VALUES ('s3://bucket/t/', %s, now() - %s::interval) RETURNING id",
        (status, completed_ago),
    ).fetchone()[0]
    if mapped:
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


def _task(db, task_type="PREPROCESS", document=None, ago="0 seconds", index=None,
          source=None):
    # dist_rag.create_source writes no document_type for CREATE_SOURCE tasks.
    details = {"document_type": "text/plain"} if task_type == "PREPROCESS" else {}
    if source:
        details["source_id"] = str(source)
    if document:
        details["document_id"] = str(document)
        # Queued tasks always carry their index; the per-index guards match on it.
        if index is None:
            doc_source = db.execute(
                "SELECT source_id FROM dist_rag.documents WHERE document_id = %s", (document,)
            ).fetchone()[0]
            index = _index_of(db, doc_source)
        details["index_id"] = str(index)
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


def _lease(db, task_id, seconds=600):
    """Put a task IN_PROGRESS under a new lease, as poll() does; return the lease token."""
    return db.execute(
        "UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS', "
        "lease_token = gen_random_uuid(), "
        "lease_expires_at = now() + %s * interval '1 second' "
        "WHERE id = %s RETURNING lease_token",
        (seconds, task_id),
    ).fetchone()[0]


def _status(db, table, key, value):
    column = "task_status" if table == "work_queue" else "status"
    return db.execute(
        f"SELECT {column} FROM dist_rag.{table} WHERE {key} = %s", (value,)
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
    def test_an_expired_lease_is_reclaimed_under_the_same_id_and_a_new_token(self, db):
        task_id = _task(db)
        poller = _poller()
        first = poller.poll(str(uuid4()), TEXT, lease_duration_seconds=1)
        time.sleep(1.5)
        second = poller.poll(str(uuid4()), TEXT)
        assert second.id == task_id and second.lease_token != first.lease_token
        assert not poller.mark_completed(task_id, lease_token=first.lease_token)
        assert poller.mark_completed(task_id, lease_token=second.lease_token)

    def test_a_live_worker_keeps_its_task_past_its_lease(self, db):
        """keep_lease renews the lease, so no poll reclaims a running task."""
        task_id = _task(db, task_type="CREATE_SOURCE")
        poller = _poller()
        task = poller.poll(str(uuid4()), TEXT, lease_duration_seconds=3)
        with poller.keep_lease(task, lease_duration_seconds=3):
            time.sleep(4.5)
            assert poller.poll(str(uuid4()), TEXT) is None
            row = db.execute(
                "SELECT task_status, lease_token FROM dist_rag.work_queue WHERE id = %s", (task_id,)
            ).fetchone()
        assert row == ("IN_PROGRESS", task.lease_token)

    def test_the_third_expiry_fails_the_task_and_its_document_together(self, db):
        doc = _document(db, _source(db))
        task_id = _task(db, document=doc)
        db.execute(
            "UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS', "
            "lease_token = gen_random_uuid(), lease_expires_at = now() - interval '1 second', "
            "task_details = task_details || '{\"expired_claims\": 2}' WHERE id = %s",
            (task_id,),
        )
        db.execute("UPDATE dist_rag.documents SET status = 'PROCESSING' WHERE document_id = %s",
                   (doc,))
        assert _poller().poll(str(uuid4()), TEXT) is None
        row = db.execute(
            "SELECT task_status, lease_token, task_details->>'expired_claims' "
            "FROM dist_rag.work_queue WHERE id = %s", (task_id,)
        ).fetchone()
        assert row == ("FAILED", None, "3")
        assert _status(db, "documents", "document_id", doc) == "FAILED"

    def test_the_third_expiry_of_a_source_task_fails_the_source(self, db):
        source = _source(db, status="IN_PROGRESS")
        task_id = _task(db, task_type="CREATE_SOURCE", source=source)
        db.execute(
            "UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS', "
            "lease_token = gen_random_uuid(), lease_expires_at = now() - interval '1 second', "
            "task_details = task_details || '{\"expired_claims\": 2}' WHERE id = %s",
            (task_id,),
        )
        assert _poller().poll(str(uuid4()), TEXT) is None
        assert _status(db, "work_queue", "id", task_id) == "FAILED"
        assert _status(db, "sources", "id", source) == "FAILED"

    def test_a_crash_before_commit_leaves_the_task_to_the_next_poll(self, db):
        doc = _document(db, _source(db))
        task_id = _task(db, document=doc)
        db.execute(
            "UPDATE dist_rag.work_queue SET task_status = 'IN_PROGRESS', "
            "lease_token = gen_random_uuid(), lease_expires_at = now() - interval '1 second', "
            "task_details = task_details || '{\"expired_claims\": 2}' WHERE id = %s",
            (task_id,),
        )
        conn = psycopg.connect(DB_URL)
        try:
            # The limit-reaching claim and its document failure, then a crash
            # (here: a rollback) before the commit.
            assert _poller()._claim_expired(conn.cursor(), str(uuid4()), TEXT, 600) is None
            conn.rollback()
        finally:
            conn.close()
        row = db.execute(
            "SELECT task_status, task_details->>'expired_claims' FROM dist_rag.work_queue "
            "WHERE id = %s", (task_id,)
        ).fetchone()
        assert row == ("IN_PROGRESS", "2")
        assert _status(db, "documents", "document_id", doc) == "QUEUED"
        assert _poller().poll(str(uuid4()), TEXT) is None
        assert _status(db, "work_queue", "id", task_id) == "FAILED"
        assert _status(db, "documents", "document_id", doc) == "FAILED"


class TestDocumentClaim:
    def test_only_one_of_two_live_tasks_claims_a_document(self, db):
        source = _source(db)
        doc = _document(db, source)
        tracking = SourceDocumentTracking()
        first = _task(db, document=doc)
        second = _task(db, document=doc, index=_index(db, source))
        first_lease, second_lease = _lease(db, first), _lease(db, second)
        assert tracking.claim_document(doc, task_id=first, lease_token=first_lease)
        assert not tracking.claim_document(doc, task_id=second, lease_token=second_lease)

    def test_a_reclaimed_task_takes_back_its_document_and_fences_the_old_worker(self, db):
        doc = _document(db, _source(db))
        tracking = SourceDocumentTracking()
        task_id = _task(db, document=doc)
        old_lease = _lease(db, task_id)
        assert tracking.claim_document(doc, task_id=task_id, lease_token=old_lease)
        new_lease = _lease(db, task_id)  # another worker reclaimed it after expiry
        assert tracking.claim_document(doc, task_id=task_id, lease_token=new_lease)
        poller = _poller()
        assert not tracking.update_document_status(doc, "FAILED", task_id=task_id,
                                                   lease_token=old_lease)
        assert not poller.mark_completed(task_id, lease_token=old_lease)
        assert not poller.release(task_id, 60, lease_token=old_lease)
        assert tracking.update_document_status(doc, "COMPLETED", task_id=task_id,
                                               lease_token=new_lease)
        assert poller.mark_completed(task_id, lease_token=new_lease)
        assert _status(db, "documents", "document_id", doc) == "COMPLETED"

    def test_a_document_whose_lease_expired_can_be_claimed_by_another_task(self, db):
        source = _source(db)
        doc = _document(db, source)
        tracking = SourceDocumentTracking()
        first = _task(db, document=doc)
        second = _task(db, document=doc, index=_index(db, source))
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

    def test_concurrent_queuing_adds_no_duplicates(self, db):
        source = _source(db)
        docs = [_document(db, source) for _ in range(200)]
        errors = []

        def queue():
            try:
                with psycopg.connect(DB_URL, autocommit=True) as conn:
                    self._queue(conn, source)
            except Exception as e:
                errors.append(str(e))

        threads = [threading.Thread(target=queue) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        assert errors == []
        assert sum(_preprocess_tasks(db, d) for d in docs) == len(docs)


class TestCrawler:
    def _metadata(self, source, uri="s3://bucket/t/a.txt"):
        return DocumentMetadata(source_id=source, document_name="a.txt", document_uri=uri,
                                document_checksum="etag", document_type="text/plain",
                                status="QUEUED")

    def test_a_crawled_document_is_queued_for_each_mapped_index(self, db):
        source = _source(db, status="IN_PROGRESS")
        _index(db, source)
        doc = SourceDocumentTracking().insert_document_metadata(self._metadata(source))
        assert _preprocess_tasks(db, doc) == 2

    def test_a_document_of_an_unmapped_source_is_inserted_without_a_task(self, db):
        source = _source(db, status="IN_PROGRESS", mapped=False)
        doc = SourceDocumentTracking().insert_document_metadata(self._metadata(source))
        assert doc is not None and _preprocess_tasks(db, doc) == 0

    def test_relisting_a_source_leaves_one_row_per_file(self, db):
        """A retried or reclaimed CREATE_SOURCE lists the same files again."""
        source = _source(db, status="IN_PROGRESS")
        tracking = SourceDocumentTracking()
        first = tracking.insert_document_metadata(self._metadata(source))
        assert tracking.insert_document_metadata(self._metadata(source)) is None
        count = db.execute("SELECT count(*) FROM dist_rag.documents WHERE source_id = %s",
                           (source,)).fetchone()[0]
        assert count == 1 and _preprocess_tasks(db, first) == 1

    def test_the_api_server_upsert_then_the_crawler_leave_one_document(self, db):
        source = _source(db, status="IN_PROGRESS")
        uri = "s3://bucket/t/a.txt"
        # RagRepo.InsertDocument's statement in the meko api-server.
        db.execute(
            "INSERT INTO dist_rag.documents "
            "(source_id, document_name, document_uri, document_checksum, document_type, "
            " document_details) VALUES (%s, 'a.txt', %s, 'etag', 'text/plain', "
            " '{\"meko\": {\"user_id\": \"u\"}}'::jsonb) "
            "ON CONFLICT (source_id, document_uri) DO UPDATE SET "
            "document_type = EXCLUDED.document_type, document_details = EXCLUDED.document_details",
            (source, uri),
        )
        assert SourceDocumentTracking().insert_document_metadata(
            self._metadata(source, uri)) is None
        rows = db.execute("SELECT document_id, document_details->'meko'->>'user_id' "
                          "FROM dist_rag.documents WHERE source_id = %s", (source,)).fetchall()
        assert [r[1] for r in rows] == ["u"]
        # Queued while the source is still being crawled, not on completion.
        assert _preprocess_tasks(db, rows[0][0]) == 1

    def test_the_crawler_and_source_completion_queue_one_task_per_document(self, db):
        source = _source(db, status="IN_PROGRESS")
        tracking = SourceDocumentTracking()
        stop = threading.Event()

        def complete():
            with psycopg.connect(DB_URL, autocommit=True) as conn:
                while not stop.is_set():
                    conn.execute(
                        "SELECT dist_rag._queue_source_documents(%s, m.index_id) "
                        "FROM dist_rag.vector_index_source_mappings m WHERE m.source_id = %s",
                        (source, source))

        thread = threading.Thread(target=complete)
        thread.start()
        try:
            docs = [tracking.insert_document_metadata(self._metadata(source, f"s3://b/{i}.txt"))
                    for i in range(50)]
        finally:
            stop.set()
            thread.join()
        assert [_preprocess_tasks(db, d) for d in docs] == [1] * 50


class TestSweep:
    def _sweep(self, poller=None):
        poller = poller or _poller()
        poller._last_sweep = 0.0
        _run(poller._maybe_sweep_orphans)
        return poller

    def test_a_document_stuck_processing_with_no_task_is_requeued(self, db):
        doc = _document(db, _source(db), status="PROCESSING")
        self._sweep()
        assert _status(db, "documents", "document_id", doc) == "QUEUED"
        assert _preprocess_tasks(db, doc) == 1

    def test_a_document_with_a_live_task_is_left_alone(self, db):
        doc = _document(db, _source(db))
        _task(db, document=doc)
        self._sweep()
        assert _preprocess_tasks(db, doc) == 1

    def test_a_live_task_for_one_index_does_not_hide_a_missing_one(self, db):
        source = _source(db)
        doc = _document(db, source)
        _task(db, document=doc)
        _index(db, source)
        self._sweep()
        assert _preprocess_tasks(db, doc) == 2

    def test_a_recently_completed_source_is_not_swept(self, db):
        doc = _document(db, _source(db, completed_ago="1 minute"))
        self._sweep()
        assert _preprocess_tasks(db, doc) == 0

    def test_only_one_worker_sweeps_per_interval(self, db):
        source = _source(db)
        first_doc = _document(db, source)
        self._sweep()
        assert _preprocess_tasks(db, first_doc) == 1
        second_doc = _document(db, source)
        self._sweep()  # another worker, same interval
        assert _preprocess_tasks(db, second_doc) == 0
        db.execute("UPDATE dist_rag.maintenance_runs SET last_run_at = 'epoch'")
        self._sweep()
        assert _preprocess_tasks(db, second_doc) == 1

    def test_a_losing_worker_does_not_wait_for_a_running_sweep(self, db):
        doc = _document(db, _source(db))
        winner = psycopg.connect(DB_URL)
        try:
            cur = winner.cursor()
            assert _poller()._win_sweep(cur)
            # The winner has committed its slot and is now sweeping, holding
            # a lock on the document it is resetting.
            cur.execute("SELECT 1 FROM dist_rag.documents WHERE document_id = %s FOR UPDATE",
                        (doc,))
            start = time.monotonic()
            self._sweep()
            assert time.monotonic() - start < 5
        finally:
            winner.rollback()
            winner.close()
