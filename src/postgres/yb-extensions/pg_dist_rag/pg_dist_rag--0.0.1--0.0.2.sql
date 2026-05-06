-- Migrate pg_dist_rag from 0.0.1 to 0.0.2.
--
-- Changes:
--   - Add dist_rag.documents.document_type column (MIME type).
--   - Propagate document_type through the PREPROCESS work_queue task_details
--     payload produced by dist_rag._queue_source_documents and
--     dist_rag.build_index.

\echo Use "ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.2'" to load this file. \quit

ALTER TABLE dist_rag.documents
    ADD COLUMN IF NOT EXISTS document_type TEXT;

CREATE OR REPLACE FUNCTION dist_rag._queue_source_documents(
    r_source_id UUID,
    r_index_id UUID
)
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_document RECORD;
    v_count INTEGER := 0;
BEGIN
    -- Validate required parameter
    IF r_source_id IS NULL THEN
        RAISE EXCEPTION 'source_id is required and cannot be NULL';
    END IF;

    -- Validate required parameter
    IF r_index_id IS NULL THEN
        RAISE EXCEPTION 'index_id is required and cannot be NULL';
    END IF;

    -- Insert work queue entries for all documents in this source
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
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status,
            'document_type', d.document_type
        ),
        NOW()
    FROM dist_rag.documents d
    WHERE d.source_id = r_source_id;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE 'Number of Documents Queued % for Preprocessing', v_count;
    RETURN v_count;

EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error queuing documents for source %: % - %', r_source_id, SQLSTATE, SQLERRM;
END;
$$;

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
    v_count INTEGER;
BEGIN
    -- Validate that exactly one parameter is provided
    IF (r_index_id IS NULL AND r_index_name IS NULL) THEN
        RAISE EXCEPTION 'Either index_id or index_name is required and cannot both be NULL';
    END IF;
    
    IF (r_index_id IS NOT NULL AND r_index_name IS NOT NULL) THEN
        RAISE EXCEPTION 'Provide only one of index_id or index_name, not both';
    END IF;

    -- If index_name is provided, resolve it to index_id
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

    -- Queue all documents for all sources associated with this index
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
            'index_id', v_index_id,
            'source_id', m.source_id,
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status,
            'document_type', d.document_type
        ),
        NOW()
    FROM dist_rag.vector_index_source_mappings m
    JOIN dist_rag.documents d ON d.source_id = m.source_id
    WHERE m.index_id = v_index_id;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE 'Index build kicked off successfully for index_id: %, documents queued: %', v_index_id, v_count;

EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error building index: % - %', SQLSTATE, SQLERRM;
END;
$$;
