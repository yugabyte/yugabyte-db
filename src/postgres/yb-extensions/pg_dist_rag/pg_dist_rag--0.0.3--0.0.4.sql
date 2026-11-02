-- pg_dist_rag 0.0.3 -> 0.0.4: keep the queue consistent instead of repairing it.

\echo Use "ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4'" to load this file. \quit

-- This script adds two unique indexes and deletes nothing. Existing duplicate
-- documents and duplicate live tasks must be removed first with
-- upgrade/dedupe_before_0.0.4.sql, and on a running cluster the final pass and
-- this upgrade run with the api-server and RAG workers stopped: see
-- "Upgrading a running cluster" in README.md.
DO $$
DECLARE
    v_documents BIGINT;
    v_tasks BIGINT;
BEGIN
    SELECT count(*) INTO v_documents FROM (
        SELECT 1 FROM dist_rag.documents
        WHERE document_uri IS NOT NULL
        GROUP BY source_id, document_uri
        HAVING count(*) > 1) g;

    SELECT count(*) INTO v_tasks FROM (
        SELECT 1 FROM dist_rag.work_queue
        WHERE task_type = 'PREPROCESS' AND task_status IN ('QUEUED', 'IN_PROGRESS')
        GROUP BY task_details->>'document_id', task_details->>'index_id'
        HAVING count(*) > 1) g;

    IF v_documents > 0 OR v_tasks > 0 THEN
        RAISE EXCEPTION 'pg_dist_rag 0.0.4 needs duplicates removed first: % duplicate document group(s), % duplicate live task group(s)',
            v_documents, v_tasks
            USING HINT = 'Run upgrade/dedupe_before_0.0.4.sql as described in "Upgrading a running cluster" in the pg_dist_rag README.';
    END IF;

    -- An online build that hits a concurrent duplicate fails and leaves the
    -- index behind, invalid. CREATE UNIQUE INDEX IF NOT EXISTS below would
    -- then skip it and leave uniqueness unenforced.
    IF EXISTS (
        SELECT 1 FROM pg_index
        WHERE indexrelid IN (to_regclass('dist_rag.uq_documents_source_uri'),
                             to_regclass('dist_rag.uq_live_preprocess'))
          AND NOT indisvalid)
    THEN
        RAISE EXCEPTION 'pg_dist_rag 0.0.4 found an invalid uq_documents_source_uri or uq_live_preprocess index left by a failed online build'
            USING HINT = 'DROP INDEX it, remove the remaining duplicates, and build it again.';
    END IF;
END;
$$;

-- One row per file in a source. The api-server's InsertDocument upsert names
-- this conflict target; the crawler inserts with a targetless ON CONFLICT DO
-- NOTHING. document_uri can be NULL, and NULLs never conflict.
CREATE UNIQUE INDEX IF NOT EXISTS uq_documents_source_uri
    ON dist_rag.documents (source_id, document_uri);

-- At most one live PREPROCESS task per document and index. Claiming a task
-- (QUEUED to IN_PROGRESS) keeps it inside the predicate, so a claim never
-- conflicts; a FAILED task leaves it, so a retry can be queued.
CREATE UNIQUE INDEX IF NOT EXISTS uq_live_preprocess
    ON dist_rag.work_queue ((task_details->>'document_id'), (task_details->>'index_id'))
    WHERE task_type = 'PREPROCESS' AND task_status IN ('QUEUED', 'IN_PROGRESS');

-- Without statistics, as right after a large ingestion, the planner picks this
-- index for the CREATE_SOURCE and PREPROCESS claims and walks every QUEUED row
-- (measured: 155-257ms per claim with 200k queued tasks, 1.2ms once it is
-- gone). idx_work_queue_status_type_doctype_created_at also leads with
-- task_status, so the claims and the sweep can use it instead.
DROP INDEX IF EXISTS dist_rag.idx_work_queue_task_status_created_at;

-- uq_live_preprocess rejects a second live task, so the NOT EXISTS anti-join
-- is gone. Its plan depended on statistics and went quadratic without them,
-- and it didn't stop two concurrent callers from both inserting.
--
-- No EXCEPTION WHEN OTHERS block: concurrent callers now conflict on
-- uq_live_preprocess, and YugabyteDB retries such a statement transparently
-- only if the error keeps its serialization SQLSTATE. Re-raising it as a new
-- exception made the conflict fatal to the caller.
CREATE OR REPLACE FUNCTION dist_rag._queue_source_documents(
    r_source_id UUID,
    r_index_id UUID
)
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_count INTEGER := 0;
    v_skipped INTEGER := 0;
    v_tenant_id UUID;
BEGIN
    IF r_source_id IS NULL THEN
        RAISE EXCEPTION 'source_id is required and cannot be NULL';
    END IF;

    IF r_index_id IS NULL THEN
        RAISE EXCEPTION 'index_id is required and cannot be NULL';
    END IF;

    SELECT tenant_id INTO v_tenant_id
    FROM dist_rag.sources
    WHERE id = r_source_id;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'Source % does not exist', r_source_id;
    END IF;

    -- PROCESSING and COMPLETED documents are skipped, so a rerun never embeds
    -- a document twice; the others are queued unless a task for them and this
    -- index is already live.
    INSERT INTO dist_rag.work_queue (
        task_type,
        task_status,
        task_details,
        created_at
    )
    SELECT
        'PREPROCESS'::dist_rag.task_type_enum,
        'QUEUED'::dist_rag.task_queue_status_enum,
        jsonb_build_object(
            'index_id', r_index_id,
            'source_id', r_source_id,
            'tenant_id', v_tenant_id,
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status,
            'document_type', d.document_type
        ),
        NOW()
    FROM dist_rag.documents d
    WHERE d.source_id = r_source_id
      AND d.status NOT IN (
            'PROCESSING'::dist_rag.document_processing_status_enum,
            'COMPLETED'::dist_rag.document_processing_status_enum
          )
    ON CONFLICT DO NOTHING;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    SELECT COUNT(*) - v_count INTO v_skipped
    FROM dist_rag.documents d
    WHERE d.source_id = r_source_id;

    RAISE NOTICE 'Documents queued: %; skipped (in flight, finished or already queued): %',
        v_count, v_skipped;
    RETURN v_count;
END;
$$;

-- Same body as 0.0.1 without the EXCEPTION WHEN OTHERS block, for the reason
-- given above _queue_source_documents: the api-server can call build_index
-- for a source while the agent queues the same source on completion.
CREATE OR REPLACE FUNCTION dist_rag.build_index(
    r_index_id UUID DEFAULT NULL,
    r_index_name VARCHAR(50) DEFAULT NULL
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_index_id UUID;
    v_source_id UUID;
    v_queued_for_source INTEGER;
    v_total_queued INTEGER := 0;
BEGIN
    IF (r_index_id IS NULL AND r_index_name IS NULL) THEN
        RAISE EXCEPTION 'Either index_id or index_name is required and cannot both be NULL';
    END IF;

    IF (r_index_id IS NOT NULL AND r_index_name IS NOT NULL) THEN
        RAISE EXCEPTION 'Provide only one of index_id or index_name, not both';
    END IF;

    IF r_index_name IS NOT NULL THEN
        SELECT id INTO v_index_id
        FROM dist_rag.vector_indexes
        WHERE index_name = r_index_name;

        IF v_index_id IS NULL THEN
            RAISE EXCEPTION 'Vector index with name "%" does not exist', r_index_name;
        END IF;
    ELSE
        v_index_id := r_index_id;
    END IF;

    FOR v_source_id IN
        SELECT source_id
        FROM dist_rag.vector_index_source_mappings
        WHERE index_id = v_index_id
    LOOP
        v_queued_for_source := dist_rag._queue_source_documents(v_source_id, v_index_id);
        v_total_queued := v_total_queued + v_queued_for_source;
    END LOOP;

    RAISE NOTICE 'Index build kicked off for index_id: %; documents queued: %',
        v_index_id, v_total_queued;
END;
$$;

-- Queue one document for every index its source is mapped to. The crawler
-- calls it in the same transaction as the document insert, so a crawled
-- document never exists without its task. Builds the same task_details as
-- _queue_source_documents.
CREATE OR REPLACE FUNCTION dist_rag._queue_document(
    r_document_id UUID
)
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_count INTEGER := 0;
BEGIN
    IF r_document_id IS NULL THEN
        RAISE EXCEPTION 'document_id is required and cannot be NULL';
    END IF;

    INSERT INTO dist_rag.work_queue (
        task_type,
        task_status,
        task_details,
        created_at
    )
    SELECT
        'PREPROCESS'::dist_rag.task_type_enum,
        'QUEUED'::dist_rag.task_queue_status_enum,
        jsonb_build_object(
            'index_id', m.index_id,
            'source_id', d.source_id,
            'tenant_id', s.tenant_id,
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status,
            'document_type', d.document_type
        ),
        NOW()
    FROM dist_rag.documents d
    JOIN dist_rag.sources s ON s.id = d.source_id
    JOIN (SELECT DISTINCT source_id, index_id
          FROM dist_rag.vector_index_source_mappings) m ON m.source_id = d.source_id
    WHERE d.document_id = r_document_id
      AND d.status NOT IN (
            'PROCESSING'::dist_rag.document_processing_status_enum,
            'COMPLETED'::dist_rag.document_processing_status_enum
          )
    ON CONFLICT DO NOTHING;

    GET DIAGNOSTICS v_count = ROW_COUNT;
    RETURN v_count;
END;
$$;

-- Agent-side periodic jobs that should run once per interval for the whole
-- cluster, not once per pod. A worker claims a run with a compare-and-set on
-- last_run_at and commits it before doing the work.
CREATE TABLE IF NOT EXISTS dist_rag.maintenance_runs (
    name TEXT PRIMARY KEY,
    last_run_at TIMESTAMP NOT NULL
);

INSERT INTO dist_rag.maintenance_runs (name, last_run_at)
VALUES ('orphan_sweep', 'epoch')
ON CONFLICT (name) DO NOTHING;
