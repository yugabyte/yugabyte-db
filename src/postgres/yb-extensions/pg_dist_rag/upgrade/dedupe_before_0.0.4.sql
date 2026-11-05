-- Removes duplicate documents and duplicate live PREPROCESS tasks before
-- ALTER EXTENSION pg_dist_rag UPDATE TO '0.0.4', whose unique indexes can't
-- build while duplicates exist. Run against a database on pg_dist_rag 0.0.3:
--
--     ysqlsh -d <db> -f upgrade/dedupe_before_0.0.4.sql
--
-- then follow "Upgrading a running cluster" in README.md, and finish with
-- DROP SCHEMA dist_rag_upgrade CASCADE.
--
-- The helpers live in their own schema, not in the extension: an extension
-- script may not CREATE OR REPLACE a function it doesn't own, and the 0.0.4
-- script only checks that no duplicates remain.
--
-- Each call is one transaction. Online, a group whose rows a worker could
-- still write (a PREPROCESS task IN_PROGRESS, or a document under a live
-- claim) is skipped. Row locks keep a worker from embedding a row being
-- deleted, but can't stop a new duplicate from being inserted, so the last
-- pass runs with quiet => true while the api-server and RAG workers are
-- stopped and every lease has expired.

CREATE SCHEMA IF NOT EXISTS dist_rag_upgrade;

CREATE TABLE IF NOT EXISTS dist_rag_upgrade.duplicate_document_groups (
    source_id UUID NOT NULL,
    document_uri TEXT NOT NULL,
    skipped_at TIMESTAMP,
    PRIMARY KEY (source_id, document_uri)
);

-- Refuses to run quiet while any lease is live: a live lease means a worker
-- is still running, so expired IN_PROGRESS tasks can't be treated as abandoned.
CREATE OR REPLACE FUNCTION dist_rag_upgrade._require_quiet()
RETURNS VOID
LANGUAGE plpgsql
AS $$
DECLARE
    v_live BIGINT;
BEGIN
    SELECT count(*) INTO v_live
    FROM dist_rag.work_queue
    WHERE task_status = 'IN_PROGRESS' AND lease_expires_at > now();
    IF v_live > 0 THEN
        RAISE EXCEPTION '% task(s) still hold a live lease; quiet => true needs every RAG worker stopped and every lease expired',
            v_live;
    END IF;
END;
$$;

-- Finds duplicate (source_id, document_uri) groups and records them for
-- dedupe_documents. Run it before the online passes and again at the start
-- of the quiet window. Returns the number of groups recorded.
CREATE OR REPLACE FUNCTION dist_rag_upgrade.find_duplicate_documents()
RETURNS BIGINT
LANGUAGE plpgsql
AS $$
DECLARE
    v_count BIGINT;
BEGIN
    INSERT INTO dist_rag_upgrade.duplicate_document_groups (source_id, document_uri)
    SELECT source_id, document_uri
    FROM dist_rag.documents
    WHERE document_uri IS NOT NULL
    GROUP BY source_id, document_uri
    HAVING count(*) > 1
    ON CONFLICT DO NOTHING;

    SELECT count(*) INTO v_count FROM dist_rag_upgrade.duplicate_document_groups;
    RETURN v_count;
END;
$$;

-- True when the table has an index led by document_id or by
-- (tenant_id, document_id), so a delete by document is an index lookup.
CREATE OR REPLACE FUNCTION dist_rag_upgrade._has_document_index(r_table REGCLASS)
RETURNS BOOLEAN
LANGUAGE sql
AS $$
    SELECT EXISTS (
        SELECT 1
        FROM pg_index i
        JOIN pg_attribute a0 ON a0.attrelid = i.indrelid AND a0.attnum = i.indkey[0]
        LEFT JOIN pg_attribute a1 ON a1.attrelid = i.indrelid AND a1.attnum = i.indkey[1]
        WHERE i.indrelid = r_table
          AND (a0.attname = 'document_id'
               OR (a0.attname = 'tenant_id' AND a1.attname = 'document_id')));
$$;

-- Cleans up to batch_size recorded duplicate groups. Within a group it keeps
-- the first row of
--
--     ORDER BY (status = 'COMPLETED') DESC,
--              COALESCE(document_details->'meko' ? 'user_id', false) DESC,
--              document_id
--
-- A COMPLETED row has all its chunks committed, so it stays searchable. Next
-- comes the api-server row: only the api-server writes meko.user_id, while
-- claim_document also writes under meko. When the keeper isn't the api-server
-- row, the api-server's uploader keys are copied into it. If no row is
-- COMPLETED, the keeper is set QUEUED and gets its own PREPROCESS tasks.
--
-- The other rows lose their tasks, pipeline rows and chunks, then are deleted.
-- A group is one source, and a source belongs to one tenant, so nothing
-- crosses a datapack: chunk deletes match the source's tenant_id or a NULL
-- tenant_id, and a loser chunk carrying any other tenant aborts the call.
CREATE OR REPLACE FUNCTION dist_rag_upgrade.dedupe_documents(
    batch_size INTEGER DEFAULT 100,
    quiet BOOLEAN DEFAULT false
)
RETURNS TABLE (
    groups_cleaned INTEGER,
    groups_skipped INTEGER,
    groups_remaining BIGINT,
    keepers_requeued INTEGER,
    documents_deleted BIGINT,
    tasks_deleted BIGINT,
    pipeline_rows_deleted BIGINT,
    chunks_deleted BIGINT,
    null_tenant_chunks_deleted BIGINT,
    tables_without_document_index TEXT[]
)
LANGUAGE plpgsql
AS $$
DECLARE
    g RECORD;
    t RECORD;
    v_ids UUID[];
    v_keeper UUID;
    v_keeper_status dist_rag.document_processing_status_enum;
    v_losers UUID[];
    v_tenant UUID;
    v_uploader JSONB;
    v_n BIGINT;
    v_conflicts BIGINT;
    v_table REGCLASS;
    v_docs UUID[];
    v_tenants UUID[];
    -- Losers of every cleaned group, with their tenant and source, for the
    -- batched chunk deletes at the end.
    v_all_losers UUID[] := '{}';
    v_all_tenants UUID[] := '{}';
    v_all_sources UUID[] := '{}';
BEGIN
    IF quiet THEN
        PERFORM dist_rag_upgrade._require_quiet();
    END IF;

    groups_cleaned := 0;
    groups_skipped := 0;
    keepers_requeued := 0;
    documents_deleted := 0;
    tasks_deleted := 0;
    pipeline_rows_deleted := 0;
    chunks_deleted := 0;
    null_tenant_chunks_deleted := 0;
    tables_without_document_index := '{}';

    FOR g IN
        SELECT source_id, document_uri
        FROM dist_rag_upgrade.duplicate_document_groups
        ORDER BY skipped_at NULLS FIRST, source_id, document_uri
        LIMIT batch_size
    LOOP
        BEGIN
            PERFORM 1 FROM dist_rag.documents
            WHERE source_id = g.source_id AND document_uri = g.document_uri
            FOR UPDATE NOWAIT;

            SELECT array_agg(document_id ORDER BY
                       (status = 'COMPLETED') DESC,
                       COALESCE(document_details->'meko' ? 'user_id', false) DESC,
                       document_id)
            INTO v_ids
            FROM dist_rag.documents
            WHERE source_id = g.source_id AND document_uri = g.document_uri;

            IF coalesce(array_length(v_ids, 1), 0) < 2 THEN
                DELETE FROM dist_rag_upgrade.duplicate_document_groups
                WHERE source_id = g.source_id AND document_uri = g.document_uri;
                CONTINUE;
            END IF;

            v_keeper := v_ids[1];
            v_losers := v_ids[2:];

            PERFORM 1 FROM dist_rag.work_queue
            WHERE task_type = 'PREPROCESS'
              AND task_details->>'document_id' = ANY(v_ids::text[])
            FOR UPDATE NOWAIT;

            -- Online, an IN_PROGRESS task is active even with an expired
            -- lease: its worker may still be committing a chunk batch, and
            -- another worker can reclaim it at any moment. Quiet, every lease
            -- has expired (checked above) and no worker is running.
            IF EXISTS (
                SELECT 1 FROM dist_rag.work_queue
                WHERE task_type = 'PREPROCESS'
                  AND task_details->>'document_id' = ANY(v_ids::text[])
                  AND task_status = 'IN_PROGRESS'
                  AND (NOT quiet OR lease_expires_at > now()))
               OR EXISTS (
                SELECT 1 FROM dist_rag.documents d
                JOIN dist_rag.work_queue w
                  ON w.id::text = d.document_details->'meko'->>'claimed_by'
                 AND w.lease_token::text = d.document_details->'meko'->>'claim_lease'
                WHERE d.document_id = ANY(v_ids)
                  AND d.status = 'PROCESSING'
                  AND w.task_status = 'IN_PROGRESS'
                  AND w.lease_expires_at > now())
            THEN
                groups_skipped := groups_skipped + 1;
                UPDATE dist_rag_upgrade.duplicate_document_groups SET skipped_at = now()
                WHERE source_id = g.source_id AND document_uri = g.document_uri;
                CONTINUE;
            END IF;

            SELECT tenant_id INTO v_tenant FROM dist_rag.sources WHERE id = g.source_id;

            SELECT status INTO v_keeper_status
            FROM dist_rag.documents WHERE document_id = v_keeper;

            SELECT document_details->'meko' INTO v_uploader
            FROM dist_rag.documents
            WHERE document_id = ANY(v_losers)
              AND COALESCE(document_details->'meko' ? 'user_id', false)
            ORDER BY document_id
            LIMIT 1;

            IF v_uploader IS NOT NULL THEN
                UPDATE dist_rag.documents
                SET document_details = jsonb_set(
                        COALESCE(document_details, '{}'::jsonb),
                        '{meko}',
                        jsonb_strip_nulls(jsonb_build_object(
                            'user_id', v_uploader->'user_id',
                            'size', v_uploader->'size',
                            'uploaded_at', v_uploader->'uploaded_at'))
                        || COALESCE(document_details->'meko', '{}'::jsonb),
                        true)
                WHERE document_id = v_keeper;
            END IF;

            DELETE FROM dist_rag.work_queue
            WHERE task_type = 'PREPROCESS'
              AND task_details->>'document_id' = ANY(v_losers::text[]);
            GET DIAGNOSTICS v_n = ROW_COUNT;
            tasks_deleted := tasks_deleted + v_n;

            DELETE FROM dist_rag.pipeline_details WHERE document_id = ANY(v_losers);
            GET DIAGNOSTICS v_n = ROW_COUNT;
            pipeline_rows_deleted := pipeline_rows_deleted + v_n;

            DELETE FROM dist_rag.documents WHERE document_id = ANY(v_losers);
            GET DIAGNOSTICS v_n = ROW_COUNT;
            documents_deleted := documents_deleted + v_n;

            -- No COMPLETED row: queue only the keeper, one task per mapped
            -- index, with the payload dist_rag._queue_document builds in 0.0.4.
            IF v_keeper_status <> 'COMPLETED' THEN
                UPDATE dist_rag.documents SET status = 'QUEUED'
                WHERE document_id = v_keeper;

                INSERT INTO dist_rag.work_queue (task_type, task_status, task_details, created_at)
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
                        'document_type', d.document_type),
                    NOW()
                FROM dist_rag.documents d
                JOIN dist_rag.sources s ON s.id = d.source_id
                JOIN (SELECT DISTINCT source_id, index_id
                      FROM dist_rag.vector_index_source_mappings) m
                  ON m.source_id = d.source_id
                WHERE d.document_id = v_keeper
                  AND NOT EXISTS (
                      SELECT 1 FROM dist_rag.work_queue w
                      WHERE w.task_type = 'PREPROCESS'
                        AND w.task_status IN ('QUEUED', 'IN_PROGRESS')
                        AND w.task_details->>'document_id' = d.document_id::text
                        AND w.task_details->>'index_id' = m.index_id::text);

                keepers_requeued := keepers_requeued + 1;
            END IF;

            DELETE FROM dist_rag_upgrade.duplicate_document_groups
            WHERE source_id = g.source_id AND document_uri = g.document_uri;
            groups_cleaned := groups_cleaned + 1;

            -- Last, so a group whose block rolled back adds nothing: plpgsql
            -- variables aren't restored by a subtransaction rollback.
            v_all_losers := v_all_losers || v_losers;
            v_all_tenants := v_all_tenants || array_fill(v_tenant, ARRAY[array_length(v_losers, 1)]);
            v_all_sources := v_all_sources || array_fill(g.source_id, ARRAY[array_length(v_losers, 1)]);
        EXCEPTION WHEN lock_not_available OR serialization_failure THEN
            groups_skipped := groups_skipped + 1;
            UPDATE dist_rag_upgrade.duplicate_document_groups SET skipped_at = now()
            WHERE source_id = g.source_id AND document_uri = g.document_uri;
        END;
    END LOOP;

    -- One check and one pair of deletes per vector table for the whole batch,
    -- so a table without a document_id index is scanned once, not per group.
    FOR t IN
        SELECT DISTINCT v.id AS index_id, v.schema_name, v.index_name
        FROM dist_rag.vector_indexes v
        JOIN dist_rag.vector_index_source_mappings m ON m.index_id = v.id
        WHERE m.source_id = ANY(v_all_sources)
    LOOP
        v_table := to_regclass(format('%I.%I', t.schema_name, t.index_name));
        CONTINUE WHEN v_table IS NULL;

        SELECT array_agg(l.doc), array_agg(l.tenant)
        INTO v_docs, v_tenants
        FROM unnest(v_all_losers, v_all_tenants, v_all_sources) AS l(doc, tenant, src)
        WHERE EXISTS (SELECT 1 FROM dist_rag.vector_index_source_mappings m
                      WHERE m.source_id = l.src AND m.index_id = t.index_id);

        IF NOT dist_rag_upgrade._has_document_index(v_table) THEN
            tables_without_document_index := tables_without_document_index || v_table::text;
        END IF;

        EXECUTE format(
            'SELECT count(*) FROM %s c JOIN unnest($1::uuid[], $2::uuid[]) AS l(doc, tenant) '
            'ON c.document_id = l.doc '
            'WHERE c.tenant_id IS NOT NULL AND c.tenant_id IS DISTINCT FROM l.tenant',
            v_table)
        INTO v_conflicts USING v_docs, v_tenants;
        IF v_conflicts > 0 THEN
            RAISE EXCEPTION '% chunk(s) in % belong to a duplicate document but carry another tenant_id; nothing was deleted',
                v_conflicts, v_table;
        END IF;

        EXECUTE format(
            'DELETE FROM %s c USING unnest($1::uuid[]) AS l(doc) '
            'WHERE c.document_id = l.doc AND c.tenant_id IS NULL',
            v_table)
        USING v_docs;
        GET DIAGNOSTICS v_n = ROW_COUNT;
        null_tenant_chunks_deleted := null_tenant_chunks_deleted + v_n;

        EXECUTE format(
            'DELETE FROM %s c USING unnest($1::uuid[], $2::uuid[]) AS l(doc, tenant) '
            'WHERE c.document_id = l.doc AND c.tenant_id = l.tenant',
            v_table)
        USING v_docs, v_tenants;
        GET DIAGNOSTICS v_n = ROW_COUNT;
        chunks_deleted := chunks_deleted + v_n;
    END LOOP;

    SELECT count(*) INTO groups_remaining FROM dist_rag_upgrade.duplicate_document_groups;
    RETURN NEXT;
END;
$$;

-- Removes duplicate live PREPROCESS tasks, keeping one per (document_id,
-- index_id) and preferring an IN_PROGRESS row. Online, a group still left
-- with two IN_PROGRESS rows is skipped: deleting either active claim isn't
-- safe. Quiet, every lease has expired, so those are removed too. The README
-- runs it only quiet: online, its scan of the live queue can exhaust YSQL's
-- restart-read retries while workers claim tasks.
--
-- A call examines at most batch_size groups, skipped ones included, so a
-- queue with many active duplicates can't turn one call into one large
-- transaction. Groups with two running tasks, which online calls skip, come
-- last, so they don't use up every batch ahead of groups that can be cleaned.
CREATE OR REPLACE FUNCTION dist_rag_upgrade.dedupe_live_tasks(
    batch_size INTEGER DEFAULT 1000,
    quiet BOOLEAN DEFAULT false
)
RETURNS TABLE (
    groups_cleaned INTEGER,
    groups_skipped INTEGER,
    groups_remaining BIGINT,
    tasks_deleted BIGINT
)
LANGUAGE plpgsql
AS $$
DECLARE
    g RECORD;
    v_ids UUID[];
    v_statuses TEXT[];
    v_n BIGINT;
BEGIN
    IF quiet THEN
        PERFORM dist_rag_upgrade._require_quiet();
    END IF;

    groups_cleaned := 0;
    groups_skipped := 0;
    tasks_deleted := 0;

    FOR g IN
        SELECT task_details->>'document_id' AS document_id,
               task_details->>'index_id' AS index_id
        FROM dist_rag.work_queue
        WHERE task_type = 'PREPROCESS' AND task_status IN ('QUEUED', 'IN_PROGRESS')
        GROUP BY 1, 2
        HAVING count(*) > 1
        ORDER BY count(*) FILTER (WHERE task_status = 'IN_PROGRESS') > 1, 1, 2
        LIMIT batch_size
    LOOP
        BEGIN
            SELECT array_agg(id ORDER BY (task_status = 'IN_PROGRESS') DESC, created_at, id),
                   array_agg(task_status::text ORDER BY (task_status = 'IN_PROGRESS') DESC, created_at, id)
            INTO v_ids, v_statuses
            FROM (SELECT id, task_status, created_at
                  FROM dist_rag.work_queue
                  WHERE task_type = 'PREPROCESS'
                    AND task_status IN ('QUEUED', 'IN_PROGRESS')
                    AND task_details->>'document_id' IS NOT DISTINCT FROM g.document_id
                    AND task_details->>'index_id' IS NOT DISTINCT FROM g.index_id
                  FOR UPDATE NOWAIT) t;

            IF coalesce(array_length(v_ids, 1), 0) < 2 THEN
                CONTINUE;
            END IF;

            -- Keep v_ids[1]. Delete the rest that are QUEUED, or any rest when quiet.
            DELETE FROM dist_rag.work_queue
            WHERE id = ANY(v_ids[2:])
              AND (quiet OR task_status = 'QUEUED');
            GET DIAGNOSTICS v_n = ROW_COUNT;
            tasks_deleted := tasks_deleted + v_n;

            IF v_n < array_length(v_ids, 1) - 1 THEN
                groups_skipped := groups_skipped + 1;
            ELSE
                groups_cleaned := groups_cleaned + 1;
            END IF;
        EXCEPTION WHEN lock_not_available OR serialization_failure THEN
            groups_skipped := groups_skipped + 1;
        END;
    END LOOP;

    SELECT count(*) INTO groups_remaining FROM (
        SELECT 1 FROM dist_rag.work_queue
        WHERE task_type = 'PREPROCESS' AND task_status IN ('QUEUED', 'IN_PROGRESS')
        GROUP BY task_details->>'document_id', task_details->>'index_id'
        HAVING count(*) > 1) d;
    RETURN NEXT;
END;
$$;
