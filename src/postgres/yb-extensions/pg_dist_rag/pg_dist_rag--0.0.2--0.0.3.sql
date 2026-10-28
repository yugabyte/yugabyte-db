-- pg_dist_rag 0.0.2 -> 0.0.3: the worker queue at scale.

\echo Use "ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.3'" to load this file. \quit

-- This script runs in one transaction, so its CREATE INDEX statements are not
-- online builds and fail with 40001 while workers write to work_queue. On a
-- running cluster, build the indexes online first: see "Upgrading a running
-- cluster" in README.md.

-- Workers claim source tasks before document tasks, and document tasks one
-- document type at a time. Each claim reads a single row from this index
-- however long the queue is, including for a worker pool that handles only
-- some document types.
CREATE INDEX IF NOT EXISTS idx_work_queue_status_type_doctype_created_at
    ON dist_rag.work_queue (task_status HASH, task_type ASC,
                            (task_details->>'document_type') ASC, created_at ASC);

-- The workers' sweep for documents left with no task reads only unfinished
-- documents, a small fraction of the table.
CREATE INDEX IF NOT EXISTS idx_documents_unfinished
    ON dist_rag.documents (status, source_id)
    WHERE status IN ('QUEUED', 'PROCESSING');

-- Finds a document's live PREPROCESS tasks with one lookup. Tasks are deleted
-- when they finish and a claim never changes these columns; only a
-- rate-limit retry rewrites task_details.
CREATE INDEX IF NOT EXISTS idx_work_queue_preprocess_document
    ON dist_rag.work_queue ((task_details->>'document_id'))
    WHERE task_type = 'PREPROCESS';

-- Queuing a source can happen more than once: the agent queues its documents
-- when the source completes, a client can call build_index, and the agent's
-- sweep re-queues documents left with no task. Skip documents that already
-- have a live task for this index so each of those adds no duplicate. The
-- check is per index: a source mapped to two indexes queues a task for each.
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

    -- PROCESSING and COMPLETED documents are skipped as before, so a rerun
    -- never embeds a document twice; FAILED, RETRY, QUEUED and NOT_STARTED
    -- are queued unless a task for them and this index is already QUEUED or
    -- IN_PROGRESS.
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
      AND NOT EXISTS (
            SELECT 1 FROM dist_rag.work_queue w
            WHERE w.task_type = 'PREPROCESS'
              AND w.task_details->>'document_id' = d.document_id::text
              AND w.task_details->>'index_id' = r_index_id::text
              AND w.task_status IN ('QUEUED', 'IN_PROGRESS')
          );

    GET DIAGNOSTICS v_count = ROW_COUNT;

    SELECT COUNT(*) - v_count INTO v_skipped
    FROM dist_rag.documents d
    WHERE d.source_id = r_source_id;

    RAISE NOTICE 'Documents queued: %; skipped (in flight, finished or already queued): %',
        v_count, v_skipped;
    RETURN v_count;

EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error queuing documents for source %: % - %', r_source_id, SQLSTATE, SQLERRM;
END;
$$;
