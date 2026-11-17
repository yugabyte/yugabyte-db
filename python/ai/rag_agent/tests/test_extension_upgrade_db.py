#!/usr/bin/env python3
"""
The pg_dist_rag 0.0.3 -> 0.0.4 upgrade against a real database: the
upgrade/dedupe_before_0.0.4.sql helpers, the 0.0.4 duplicate check, and the
agent running across the upgrade. Skipped unless RAG_TEST_DATABASE_URL is set.

Each test drops pg_dist_rag and installs 0.0.3, so the database must be a
throwaway one with the extension's SQL files up to 0.0.4 installed. The module
leaves the extension at its default version.

    RAG_TEST_DATABASE_URL=postgresql://yugabyte@localhost:15433/agentdb \
        pytest tests/test_extension_upgrade_db.py
"""

import os
import threading
import time
from pathlib import Path
from unittest.mock import patch
from uuid import UUID, uuid4

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

HELPER = (Path(__file__).resolve().parents[4] / "src" / "postgres" / "yb-extensions"
          / "pg_dist_rag" / "upgrade" / "dedupe_before_0.0.4.sql")
TENANT = UUID("aaaaaaaa-0000-0000-0000-000000000001")
OTHER_TENANT = UUID("bbbbbbbb-0000-0000-0000-000000000002")
UPLOADER = {"meko": {"user_id": "uploader", "size": 5, "uploaded_at": "2026-10-26T00:00:00Z"}}
CLAIMED = {"meko": {"claimed_by": str(uuid4()), "claim_lease": str(uuid4())}}


@pytest.fixture(scope="module", autouse=True)
def pool():
    ConnectionPool.initialize(DB_URL, min_size=1, max_size=10)
    yield
    ConnectionPool.close_all()
    with psycopg.connect(DB_URL, autocommit=True) as conn:
        conn.execute("DROP SCHEMA IF EXISTS dist_rag_upgrade CASCADE")
        conn.execute("DROP EXTENSION IF EXISTS pg_dist_rag CASCADE")
        conn.execute("CREATE EXTENSION pg_dist_rag")


@pytest.fixture
def db():
    """A database on pg_dist_rag 0.0.3 with the cleanup helpers installed."""
    conn = psycopg.connect(DB_URL, autocommit=True)
    conn.execute("DROP SCHEMA IF EXISTS dist_rag_upgrade CASCADE")
    conn.execute("DROP EXTENSION IF EXISTS pg_dist_rag CASCADE")
    # A schema left behind without the extension makes CREATE EXTENSION fail
    # with "schema dist_rag is not a member of extension".
    conn.execute("DROP SCHEMA IF EXISTS dist_rag CASCADE")
    conn.execute("CREATE EXTENSION pg_dist_rag VERSION '0.0.3'")
    conn.execute(HELPER.read_text())
    Capabilities.reset()
    yield conn
    conn.close()


def _vector_table(db):
    """A chunk table shaped like an extension-created one (no document_id index)."""
    name = f"kb_{uuid4().hex[:10]}"
    db.execute(f"CREATE TABLE public.{name} (id UUID PRIMARY KEY DEFAULT gen_random_uuid(), "
               f"chunk_text TEXT, document_id UUID, tenant_id UUID)")
    return name


def _source(db, tenant=TENANT, table=None, status="COMPLETED", index=None):
    """A source mapped to a new index and chunk table, or to an existing index."""
    source = db.execute(
        "INSERT INTO dist_rag.sources (source_uri, tenant_id, status, completed_at) "
        "VALUES ('s3://bucket/t/', %s, %s, now() - interval '10 minutes') RETURNING id",
        (tenant, status),
    ).fetchone()[0]
    if index is None:
        table = table or _vector_table(db)
        index = db.execute(
            "INSERT INTO dist_rag.vector_indexes (index_name, schema_name, "
            "index_creation_status, ai_provider) VALUES (%s, 'public', 'INIT', 'AWS_BEDROCK') "
            "RETURNING id",
            (table,),
        ).fetchone()[0]
    db.execute("INSERT INTO dist_rag.vector_index_source_mappings (index_id, source_id) "
               "VALUES (%s, %s)", (index, source))
    return source, table, index


def _document(db, source, status, details=None, uri="s3://bucket/t/a.txt", doc_id=None):
    return db.execute(
        "INSERT INTO dist_rag.documents (document_id, source_id, document_name, document_uri, "
        "status, document_type, document_details) VALUES (%s, %s, 'a.txt', %s, %s, "
        "'text/plain', %s) RETURNING document_id",
        (doc_id or uuid4(), source, uri, status,
         psycopg.types.json.Jsonb(details) if details is not None else None),
    ).fetchone()[0]


def _chunk(db, table, doc, tenant=TENANT, text="chunk"):
    db.execute(f"INSERT INTO public.{table} (chunk_text, document_id, tenant_id) "
               f"VALUES (%s, %s, %s)", (text, doc, tenant))


def _task(db, doc, index, status="QUEUED", lease_seconds=None):
    details = {"document_type": "text/plain", "document_id": str(doc), "index_id": str(index)}
    return db.execute(
        "INSERT INTO dist_rag.work_queue (task_type, task_status, task_details, lease_token, "
        "lease_expires_at) VALUES ('PREPROCESS', %s, %s, "
        "CASE WHEN %s::int IS NULL THEN NULL ELSE gen_random_uuid() END, "
        "now() + %s::int * interval '1 second') RETURNING id",
        (status, psycopg.types.json.Jsonb(details), lease_seconds, lease_seconds),
    ).fetchone()[0]


def _dedupe(db, quiet=False, batch=100):
    db.execute("SELECT dist_rag_upgrade.find_duplicate_documents()")
    row = db.execute("SELECT * FROM dist_rag_upgrade.dedupe_documents(%s, %s)",
                     (batch, quiet)).fetchone()
    names = ["groups_cleaned", "groups_skipped", "groups_remaining", "keepers_requeued",
             "documents_deleted", "tasks_deleted", "pipeline_rows_deleted", "chunks_deleted",
             "null_tenant_chunks_deleted", "tables_without_document_index"]
    return dict(zip(names, row))


def _docs(db, source):
    return db.execute(
        "SELECT document_id, status, document_details FROM dist_rag.documents "
        "WHERE source_id = %s", (source,)).fetchall()


def _chunks(db, table):
    return sorted(r[0] for r in db.execute(f"SELECT chunk_text FROM public.{table}").fetchall())


def _live_tasks(db, doc):
    return db.execute(
        "SELECT task_details->>'index_id' FROM dist_rag.work_queue WHERE task_type = 'PREPROCESS' "
        "AND task_status IN ('QUEUED', 'IN_PROGRESS') AND task_details->>'document_id' = %s",
        (str(doc),)).fetchall()


class TestKeeper:
    def test_the_only_completed_copy_is_kept_and_gains_the_uploader(self, db):
        source, table, _ = _source(db)
        api = _document(db, source, "FAILED", UPLOADER)
        crawler = _document(db, source, "COMPLETED", CLAIMED)
        _chunk(db, table, api, text="api")
        _chunk(db, table, crawler, text="crawler")
        result = _dedupe(db)
        assert result["groups_cleaned"] == 1 and result["chunks_deleted"] == 1
        [(doc, status, details)] = _docs(db, source)
        assert (doc, status) == (crawler, "COMPLETED")
        assert details["meko"]["user_id"] == "uploader"
        assert details["meko"]["claimed_by"] == CLAIMED["meko"]["claimed_by"]
        assert _chunks(db, table) == ["crawler"]

    def test_when_both_are_completed_the_api_server_row_is_kept(self, db):
        source, table, _ = _source(db)
        crawler = _document(db, source, "COMPLETED", CLAIMED)
        api = _document(db, source, "COMPLETED", {"meko": {**UPLOADER["meko"],
                                                           **CLAIMED["meko"]}})
        _chunk(db, table, api, text="api")
        _chunk(db, table, crawler, text="crawler")
        _dedupe(db)
        assert [d for d, _, _ in _docs(db, source)] == [api]
        assert _chunks(db, table) == ["api"]

    def test_with_no_completed_copy_only_the_keeper_is_requeued(self, db):
        source, _, index = _source(db)
        second_index = _source(db)[2]
        db.execute("INSERT INTO dist_rag.vector_index_source_mappings (index_id, source_id) "
                   "VALUES (%s, %s)", (second_index, source))
        api = _document(db, source, "FAILED", UPLOADER)
        _document(db, source, "PROCESSING", None)
        other = _document(db, source, "QUEUED", None, uri="s3://bucket/t/other.txt")
        result = _dedupe(db)
        assert result["keepers_requeued"] == 1
        [(doc, status, _)] = [r for r in _docs(db, source) if r[0] != other]
        assert (doc, status) == (api, "QUEUED")
        assert sorted(i for (i,) in _live_tasks(db, api)) == sorted([str(index),
                                                                    str(second_index)])
        assert _live_tasks(db, other) == []

    def test_two_completed_crawler_copies_keep_the_lower_document_id(self, db):
        source, _, _ = _source(db)
        low, high = UUID(int=1), UUID(int=2)
        _document(db, source, "COMPLETED", CLAIMED, doc_id=high)
        _document(db, source, "COMPLETED", None, doc_id=low)
        _dedupe(db)
        assert [d for d, _, _ in _docs(db, source)] == [low]

    def test_a_losers_tasks_and_pipeline_rows_go_with_it(self, db):
        source, _, index = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "FAILED", None)
        _task(db, loser, index, status="FAILED")
        db.execute("INSERT INTO dist_rag.pipeline_details (document_id, document_name, "
                   "metadata_snapshot) VALUES (%s, 'a.txt', '{}')", (loser,))
        result = _dedupe(db)
        assert (result["tasks_deleted"], result["pipeline_rows_deleted"]) == (1, 1)
        assert _dedupe(db)["groups_cleaned"] == 0, "a second run is a no-op"


class TestTenants:
    def test_null_tenant_chunks_of_a_loser_are_deleted_and_counted(self, db):
        source, table, _ = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "COMPLETED", None)
        _chunk(db, table, loser, tenant=None, text="legacy")
        _chunk(db, table, loser, text="tenant")
        result = _dedupe(db)
        assert (result["chunks_deleted"], result["null_tenant_chunks_deleted"]) == (1, 1)
        assert result["tables_without_document_index"] == [f"{table}"]
        assert _chunks(db, table) == []

    def test_a_chunk_with_another_tenant_aborts_and_deletes_nothing(self, db):
        source, table, _ = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "COMPLETED", None)
        _chunk(db, table, loser, tenant=OTHER_TENANT, text="foreign")
        db.execute("SELECT dist_rag_upgrade.find_duplicate_documents()")
        with pytest.raises(psycopg.errors.RaiseException, match="another tenant_id"):
            db.execute("SELECT * FROM dist_rag_upgrade.dedupe_documents()")
        assert len(_docs(db, source)) == 2 and _chunks(db, table) == ["foreign"]

    def test_the_same_file_in_two_datapacks_is_not_a_duplicate(self, db):
        # Meko maps every datapack's source to one shared index and chunk table.
        mine, table, index = _source(db)
        theirs, _, _ = _source(db, tenant=OTHER_TENANT, table=table, index=index)
        _document(db, mine, "COMPLETED", UPLOADER)
        loser = _document(db, mine, "COMPLETED", None)
        their_doc = _document(db, theirs, "COMPLETED", UPLOADER)
        _chunk(db, table, loser, text="mine-dup")
        _chunk(db, table, their_doc, tenant=OTHER_TENANT, text="theirs")
        before = _docs(db, theirs)
        result = _dedupe(db)
        assert result["groups_cleaned"] == 1 and result["documents_deleted"] == 1
        assert _docs(db, theirs) == before
        assert _chunks(db, table) == ["theirs"]


class TestActiveGroups:
    def test_a_group_with_an_in_progress_task_is_skipped_until_it_finishes(self, db):
        source, _, index = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "PROCESSING", None)
        task = _task(db, loser, index, status="IN_PROGRESS", lease_seconds=-60)
        before = _docs(db, source)
        result = _dedupe(db)
        assert (result["groups_cleaned"], result["groups_skipped"]) == (0, 1)
        assert _docs(db, source) == before, "an expired lease still counts online"
        db.execute("DELETE FROM dist_rag.work_queue WHERE id = %s", (task,))
        assert _dedupe(db)["groups_cleaned"] == 1

    def test_a_locked_group_is_skipped_without_waiting(self, db):
        source, _, _ = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        _document(db, source, "FAILED", None)
        holder = psycopg.connect(DB_URL)
        try:
            holder.execute("SELECT 1 FROM dist_rag.documents WHERE source_id = %s FOR UPDATE",
                           (source,))
            start = time.monotonic()
            result = _dedupe(db)
            assert time.monotonic() - start < 10
            assert result["groups_skipped"] == 1
        finally:
            holder.rollback()
            holder.close()

    def test_a_worker_cannot_claim_a_loser_the_helper_is_deleting(self, db):
        ConnectionPool.close_all()
        ConnectionPool.initialize(DB_URL, min_size=1, max_size=10)
        source, _, _ = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "QUEUED", None)
        helper = psycopg.connect(DB_URL)
        result = {}
        try:
            helper.execute("SELECT dist_rag_upgrade.find_duplicate_documents()")
            helper.execute("SELECT * FROM dist_rag_upgrade.dedupe_documents()")

            def claim():
                try:
                    result["claimed"] = SourceDocumentTracking().claim_document(
                        loser, task_id=uuid4(), lease_token=uuid4())
                except psycopg.Error as e:
                    result["error"] = type(e).__name__

            thread = threading.Thread(target=claim)
            thread.start()
            time.sleep(1)
            helper.commit()
            thread.join(30)
        finally:
            helper.close()
        assert result.get("claimed") is not True
        assert _docs(db, source)[0][0] != loser


class TestQuiet:
    def test_quiet_refuses_while_a_lease_is_live(self, db):
        source, _, index = _source(db)
        doc = _document(db, source, "PROCESSING", None)
        _task(db, doc, index, status="IN_PROGRESS", lease_seconds=600)
        with pytest.raises(psycopg.errors.RaiseException, match="live lease"):
            _dedupe(db, quiet=True)

    def test_quiet_cleans_groups_whose_leases_expired(self, db):
        source, _, index = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        loser = _document(db, source, "PROCESSING", None)
        _task(db, loser, index, status="IN_PROGRESS", lease_seconds=-60)
        result = _dedupe(db, quiet=True)
        assert (result["groups_cleaned"], result["groups_remaining"]) == (1, 0)


class TestLiveTasks:
    def _dedupe_tasks(self, db, quiet=False):
        return db.execute("SELECT * FROM dist_rag_upgrade.dedupe_live_tasks(1000, %s)",
                          (quiet,)).fetchone()

    def test_queued_duplicates_are_removed_keeping_the_running_task(self, db):
        source, _, index = _source(db)
        a = _document(db, source, "QUEUED", None, uri="s3://b/a")
        b = _document(db, source, "PROCESSING", None, uri="s3://b/b")
        _task(db, a, index)
        _task(db, a, index)
        running = _task(db, b, index, status="IN_PROGRESS", lease_seconds=600)
        _task(db, b, index)
        assert self._dedupe_tasks(db) == (2, 0, 0, 2)
        assert len(_live_tasks(db, a)) == 1
        assert db.execute("SELECT id FROM dist_rag.work_queue WHERE task_details->>"
                          "'document_id' = %s", (str(b),)).fetchall() == [(running,)]

    def test_two_running_tasks_are_left_online_and_removed_quiet(self, db):
        source, _, index = _source(db)
        doc = _document(db, source, "PROCESSING", None)
        _task(db, doc, index, status="IN_PROGRESS", lease_seconds=-60)
        _task(db, doc, index, status="IN_PROGRESS", lease_seconds=-30)
        assert self._dedupe_tasks(db) == (0, 1, 1, 0)
        assert self._dedupe_tasks(db, quiet=True) == (1, 0, 0, 1)

    def test_skipped_groups_count_against_the_batch_and_come_last(self, db):
        source, _, index = _source(db)
        for i in range(3):
            busy = _document(db, source, "PROCESSING", None, uri=f"s3://b/busy{i}")
            _task(db, busy, index, status="IN_PROGRESS", lease_seconds=600)
            _task(db, busy, index, status="IN_PROGRESS", lease_seconds=600)
        idle = _document(db, source, "QUEUED", None, uri="s3://b/idle")
        _task(db, idle, index)
        _task(db, idle, index)
        result = db.execute("SELECT * FROM dist_rag_upgrade.dedupe_live_tasks(2)").fetchone()
        assert result == (1, 1, 3, 1)


class TestUpgrade:
    def test_the_upgrade_refuses_while_duplicates_remain(self, db):
        source, _, _ = _source(db)
        _document(db, source, "COMPLETED", UPLOADER)
        _document(db, source, "COMPLETED", None)
        with pytest.raises(psycopg.errors.RaiseException, match="1 duplicate document group"):
            db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
        assert len(_docs(db, source)) == 2, "the check deletes nothing"
        _dedupe(db)
        db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
        db.execute("DROP SCHEMA dist_rag_upgrade CASCADE")
        assert db.execute("SELECT extversion FROM pg_extension WHERE extname = 'pg_dist_rag'"
                          ).fetchone()[0] == "0.0.4"
        assert db.execute("SELECT to_regprocedure('dist_rag._queue_document(uuid)') IS NOT NULL"
                          ).fetchone()[0]

    def test_the_upgrade_refuses_an_index_a_failed_online_build_left_invalid(self, db):
        db.execute("CREATE UNIQUE INDEX uq_documents_source_uri "
                   "ON dist_rag.documents (source_id, document_uri)")
        # What a build that met a concurrent duplicate leaves behind.
        db.execute("UPDATE pg_index SET indisvalid = false "
                   "WHERE indexrelid = 'dist_rag.uq_documents_source_uri'::regclass")
        with pytest.raises(psycopg.errors.RaiseException, match="invalid"):
            db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
        db.execute("DROP INDEX dist_rag.uq_documents_source_uri")
        db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")

    def test_the_helper_requeues_with_the_payload_queue_document_builds(self, db):
        source, _, _ = _source(db)
        keeper = _document(db, source, "FAILED", UPLOADER)
        _document(db, source, "FAILED", None)
        _dedupe(db)
        helper_payload = db.execute(
            "SELECT task_details FROM dist_rag.work_queue WHERE task_details->>'document_id' = %s",
            (str(keeper),)).fetchone()[0]
        db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
        db.execute("DELETE FROM dist_rag.work_queue")
        assert db.execute("SELECT dist_rag._queue_document(%s)", (keeper,)).fetchone()[0] == 1
        queued_payload = db.execute("SELECT task_details FROM dist_rag.work_queue").fetchone()[0]
        assert helper_payload == queued_payload

    def test_an_old_plain_insert_crawler_fails_once_the_index_exists(self, db):
        """Why the D59494 crawler must be on every pod before 0.0.4."""
        source, _, _ = _source(db)
        db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
        _document(db, source, "QUEUED", UPLOADER)
        with pytest.raises(psycopg.errors.UniqueViolation):
            db.execute("INSERT INTO dist_rag.documents (source_id, document_name, document_uri, "
                       "status) VALUES (%s, 'a.txt', 's3://bucket/t/a.txt', 'QUEUED')", (source,))
        metadata = DocumentMetadata(source_id=source, document_name="a.txt",
                                    document_uri="s3://bucket/t/a.txt", document_checksum="e",
                                    document_type="text/plain", status="QUEUED")
        assert SourceDocumentTracking().insert_document_metadata(metadata) is None


class TestAgentAcrossTheUpgrade:
    def _metadata(self, source, name):
        return DocumentMetadata(source_id=source, document_name=name,
                                document_uri=f"s3://bucket/t/{name}", document_checksum="e",
                                document_type="text/plain", status="QUEUED")

    def test_a_running_agent_switches_to_the_0_0_4_objects_without_a_restart(self, db):
        source, _, _ = _source(db, status="IN_PROGRESS")
        tracking = SourceDocumentTracking()
        with patch.object(Capabilities, "RECHECK_SECONDS", 0.5):
            on_003 = tracking.insert_document_metadata(self._metadata(source, "before"))
            assert _live_tasks(db, on_003) == [], "0.0.3: queued later, on completion"
            conn = ConnectionPool.get_connection()
            try:
                assert Poller()._win_sweep(conn.cursor()), "0.0.3: every worker sweeps"
                conn.rollback()
            finally:
                ConnectionPool.return_connection(conn)

            db.execute("ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'")
            time.sleep(1)

            on_004 = tracking.insert_document_metadata(self._metadata(source, "after"))
            assert len(_live_tasks(db, on_004)) == 1, "0.0.4: queued with the insert"
            wins = []
            for _ in range(2):
                conn = ConnectionPool.get_connection()
                try:
                    wins.append(Poller()._win_sweep(conn.cursor()))
                finally:
                    ConnectionPool.return_connection(conn)
            assert wins == [True, False], "0.0.4: one worker per interval"
