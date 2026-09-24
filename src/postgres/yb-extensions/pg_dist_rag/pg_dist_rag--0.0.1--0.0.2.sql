-- pg_dist_rag 0.0.1 -> 0.0.2 upgrade script.
-- ============================================
-- AUTO_COLUMN_EMBEDDING: registration-based, generic "source table.column
-- (text) -> destination table.column (vector)" auto-embedding, independent
-- of dist_rag.sources / dist_rag.documents. Mirrors the existing two-step
-- create_source -> init_vector_index UX rather than one combined call:
--   1. dist_rag.create_column_embedding_mapping(...) declares the source ->
--      destination mapping (like create_source declares a source) and
--      returns a mapping_id. Not yet discoverable/pollable at this point --
--      vector_index_id is still NULL.
--   2. dist_rag.init_column_embedding(r_mapping_id, r_ai_provider,
--      r_embedding_model_params) (like init_vector_index) supplies the
--      embedding-model config, creates the dist_rag.vector_indexes row for
--      it, and activates the mapping. Unlike init_vector_index, this never
--      creates a physical backing table -- the destination table already
--      exists; only vector_indexes' config-storage role is reused, never
--      its table-creation side effect.
-- From then on, matching rows are discovered and embedded automatically --
-- no per-row enqueue call, no dist_rag.work_queue task, ever. The only
-- thing a destination table needs is the nullable embedding column it
-- already needs anyway (embedding_column IS NULL is the entire discovery
-- signal). Consumed by a dedicated rag_agent worker type
-- (WORKER_TYPE=AUTO_COLUMN_EMBEDDING), not by the CREATE_SOURCE/PREPROCESS
-- document-ingestion pipeline.
-- ============================================

CREATE TYPE dist_rag.column_embedding_registration_status_enum AS ENUM ('ACTIVE', 'PAUSED');

CREATE TABLE dist_rag.column_embedding_registrations (
  id                              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  registration_name               TEXT NOT NULL,
  status                          dist_rag.column_embedding_registration_status_enum NOT NULL DEFAULT 'PAUSED',

  -- NULL until init_column_embedding runs -- a mapping declared by
  -- create_column_embedding_mapping but not yet initialized has no model
  -- config yet, and RegistrationCache's query (an INNER JOIN to
  -- vector_indexes) naturally excludes it from polling until this is set.
  -- ai_provider + embedding_model_params come from this row, read-only --
  -- never via init_vector_index's table-creating path.
  vector_index_id                 UUID REFERENCES dist_rag.vector_indexes(id),

  destination_schema              TEXT NOT NULL,
  destination_table               TEXT NOT NULL,
  destination_pk_column           TEXT NOT NULL DEFAULT 'id',
  destination_embedding_column    TEXT NOT NULL,   -- the ONLY thing that must exist on the destination table
  destination_tenant_column       TEXT,             -- optional, fairness bucketing only
  destination_priority_column     TEXT,             -- optional, claim ordering only
  destination_stale_claim_seconds INTEGER NOT NULL DEFAULT 900,
  destination_claim_batch_size    INTEGER NOT NULL DEFAULT 25,
  destination_reserved_backfill_slots INTEGER NOT NULL DEFAULT 5,

  source_connection         TEXT NOT NULL DEFAULT 'generic',
  source_schema             TEXT NOT NULL,
  source_table              TEXT NOT NULL,
  source_text_columns       TEXT[] NOT NULL,
  source_filter_by_columns  JSONB NOT NULL,   -- values derived from the claimed row, not just literals
  source_order_by           JSONB,

  created_at TIMESTAMP NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMP NOT NULL DEFAULT NOW(),
  created_by TEXT
);

CREATE UNIQUE INDEX idx_column_embedding_registrations_name ON dist_rag.column_embedding_registrations(registration_name);
CREATE INDEX idx_column_embedding_registrations_active ON dist_rag.column_embedding_registrations(status) WHERE status = 'ACTIVE';

CREATE TABLE dist_rag.column_embedding_progress (
  registration_id     UUID NOT NULL REFERENCES dist_rag.column_embedding_registrations(id),
  dest_row_pk_in_text TEXT NOT NULL,   -- destination row's PK value, as text -- generic across UUID/int/etc.
  status              TEXT NOT NULL DEFAULT 'QUEUED' CHECK (status IN ('QUEUED','IN_PROGRESS','FAILED')),
  claimed_by          TEXT,
  claimed_at          TIMESTAMP,
  attempts            SMALLINT NOT NULL DEFAULT 0,
  next_retry_at       TIMESTAMP,
  last_error          TEXT,
  updated_at          TIMESTAMP NOT NULL DEFAULT NOW(),
  PRIMARY KEY (registration_id, dest_row_pk_in_text)
);

-- Speeds up column_embedding_registration_stats' per-registration COUNT(*)
-- FILTER aggregation; the PK above already covers claim/finalize lookups.
CREATE INDEX idx_column_embedding_progress_registration_status ON dist_rag.column_embedding_progress(registration_id, status);

CREATE OR REPLACE FUNCTION dist_rag.create_column_embedding_mapping(
    r_registration_name                   TEXT,
    r_destination_schema                  TEXT,
    r_destination_table                   TEXT,
    r_destination_embedding_column        TEXT,
    r_source_schema                       TEXT,
    r_source_table                        TEXT,
    r_source_text_columns                 TEXT[],
    r_source_filter_by_columns             JSONB,
    r_destination_pk_column               TEXT DEFAULT 'id',
    r_destination_tenant_column           TEXT DEFAULT NULL,
    r_destination_priority_column         TEXT DEFAULT NULL,
    r_destination_stale_claim_seconds     INTEGER DEFAULT 900,
    r_destination_claim_batch_size        INTEGER DEFAULT 25,
    r_destination_reserved_backfill_slots INTEGER DEFAULT 5,
    r_source_connection                   TEXT DEFAULT 'generic',
    r_source_order_by                     JSONB DEFAULT NULL,
    r_created_by                          TEXT DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = dist_rag, pg_catalog
AS $$
DECLARE
    v_id UUID;
    v_filter JSONB;
    v_value_sources INTEGER;
    v_resolve_where_sources INTEGER;
BEGIN
    IF r_registration_name IS NULL OR r_registration_name = '' THEN
        RAISE EXCEPTION 'registration_name is required and cannot be NULL or empty';
    END IF;
    IF r_destination_schema IS NULL OR r_destination_schema = '' THEN
        RAISE EXCEPTION 'destination_schema is required and cannot be NULL or empty';
    END IF;
    IF r_destination_table IS NULL OR r_destination_table = '' THEN
        RAISE EXCEPTION 'destination_table is required and cannot be NULL or empty';
    END IF;
    IF r_destination_embedding_column IS NULL OR r_destination_embedding_column = '' THEN
        RAISE EXCEPTION 'destination_embedding_column is required and cannot be NULL or empty';
    END IF;
    IF r_source_schema IS NULL OR r_source_schema = '' THEN
        RAISE EXCEPTION 'source_schema is required and cannot be NULL or empty';
    END IF;
    IF r_source_table IS NULL OR r_source_table = '' THEN
        RAISE EXCEPTION 'source_table is required and cannot be NULL or empty';
    END IF;
    IF r_source_text_columns IS NULL OR array_length(r_source_text_columns, 1) IS NULL THEN
        RAISE EXCEPTION 'source_text_columns is required and cannot be NULL or empty';
    END IF;
    IF r_source_filter_by_columns IS NULL OR jsonb_typeof(r_source_filter_by_columns) != 'array' THEN
        RAISE EXCEPTION 'source_filter_by_columns is required and must be a JSON array (may be empty)';
    END IF;

    -- Each filter entry needs exactly one value source: a literal
    -- "value", a "target_value_from_column" derived from the claimed row, or a
    -- "resolve" sub-spec (itself needing exactly one of "where_value" /
    -- "where_value_from_row_column").
    FOR v_filter IN SELECT * FROM jsonb_array_elements(r_source_filter_by_columns) LOOP
        IF v_filter ->> 'source_filter_on_column' IS NULL THEN
            RAISE EXCEPTION 'each source_filter_by_columns entry requires a "source_filter_on_column" key: %', v_filter;
        END IF;

        v_value_sources := (CASE WHEN v_filter ? 'value' THEN 1 ELSE 0 END)
                          + (CASE WHEN v_filter ? 'target_value_from_column' THEN 1 ELSE 0 END)
                          + (CASE WHEN v_filter ? 'resolve' THEN 1 ELSE 0 END);
        IF v_value_sources != 1 THEN
            RAISE EXCEPTION 'each source_filter_by_columns entry needs exactly one of "value"/"target_value_from_column"/"resolve": %', v_filter;
        END IF;

        IF v_filter ? 'resolve' THEN
            IF NOT (v_filter -> 'resolve' ? 'schema' AND v_filter -> 'resolve' ? 'table'
                    AND v_filter -> 'resolve' ? 'select_column' AND v_filter -> 'resolve' ? 'where_column') THEN
                RAISE EXCEPTION 'filter entry''s "resolve" requires schema/table/select_column/where_column: %', v_filter;
            END IF;

            v_resolve_where_sources := (CASE WHEN v_filter -> 'resolve' ? 'where_value' THEN 1 ELSE 0 END)
                                      + (CASE WHEN v_filter -> 'resolve' ? 'where_value_from_row_column' THEN 1 ELSE 0 END);
            IF v_resolve_where_sources != 1 THEN
                RAISE EXCEPTION 'filter entry''s "resolve" needs exactly one of "where_value"/"where_value_from_row_column": %', v_filter;
            END IF;
        END IF;
    END LOOP;

    INSERT INTO dist_rag.column_embedding_registrations (
        registration_name,
        destination_schema, destination_table, destination_pk_column, destination_embedding_column,
        destination_tenant_column, destination_priority_column,
        destination_stale_claim_seconds, destination_claim_batch_size, destination_reserved_backfill_slots,
        source_connection, source_schema, source_table, source_text_columns, source_filter_by_columns, source_order_by,
        created_by
    )
    VALUES (
        r_registration_name,
        r_destination_schema, r_destination_table, r_destination_pk_column, r_destination_embedding_column,
        r_destination_tenant_column, r_destination_priority_column,
        r_destination_stale_claim_seconds, r_destination_claim_batch_size, r_destination_reserved_backfill_slots,
        r_source_connection, r_source_schema, r_source_table, r_source_text_columns, r_source_filter_by_columns, r_source_order_by,
        r_created_by
    )
    -- vector_index_id / status are deliberately excluded from this UPDATE
    -- SET: re-running this call (e.g. to bump destination_claim_batch_size)
    -- must not un-initialize a mapping that init_column_embedding already
    -- activated.
    ON CONFLICT (registration_name) DO UPDATE SET
        destination_schema                  = EXCLUDED.destination_schema,
        destination_table                   = EXCLUDED.destination_table,
        destination_pk_column               = EXCLUDED.destination_pk_column,
        destination_embedding_column        = EXCLUDED.destination_embedding_column,
        destination_tenant_column           = EXCLUDED.destination_tenant_column,
        destination_priority_column         = EXCLUDED.destination_priority_column,
        destination_stale_claim_seconds     = EXCLUDED.destination_stale_claim_seconds,
        destination_claim_batch_size        = EXCLUDED.destination_claim_batch_size,
        destination_reserved_backfill_slots = EXCLUDED.destination_reserved_backfill_slots,
        source_connection                   = EXCLUDED.source_connection,
        source_schema                       = EXCLUDED.source_schema,
        source_table                        = EXCLUDED.source_table,
        source_text_columns                 = EXCLUDED.source_text_columns,
        source_filter_by_columns            = EXCLUDED.source_filter_by_columns,
        source_order_by                     = EXCLUDED.source_order_by,
        updated_at                          = NOW()
    RETURNING id INTO v_id;

    RETURN v_id;
EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error creating column embedding mapping "%": % - %', r_registration_name, SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.create_column_embedding_mapping(
    TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT[], JSONB, TEXT, TEXT, TEXT, INTEGER, INTEGER, INTEGER, TEXT, JSONB, TEXT
)
IS 'declare a source table.column -> destination table.column auto-embedding mapping. Returns a mapping_id.';

CREATE OR REPLACE FUNCTION dist_rag.init_column_embedding(
    r_mapping_id            UUID,
    r_ai_provider           dist_rag.ai_provider_enum,
    r_embedding_model_params JSONB
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = dist_rag, pg_catalog
AS $$
DECLARE
    v_mapping RECORD;
    v_index_id UUID;
BEGIN
    IF r_mapping_id IS NULL THEN
        RAISE EXCEPTION 'mapping_id is required and cannot be NULL';
    END IF;

    SELECT * INTO v_mapping FROM dist_rag.column_embedding_registrations WHERE id = r_mapping_id;
    IF NOT FOUND THEN
        RAISE EXCEPTION 'No column embedding mapping with id "%" -- call create_column_embedding_mapping first', r_mapping_id;
    END IF;
    IF v_mapping.vector_index_id IS NOT NULL THEN
        RAISE EXCEPTION 'Mapping "%" is already initialized (vector_index_id %) -- use dist_rag.set_column_embedding_registration_status to pause/resume, or create a new mapping instead', v_mapping.registration_name, v_mapping.vector_index_id;
    END IF;

    -- Same validation init_vector_index applies to its own embedding_model_params.
    IF r_embedding_model_params IS NULL OR r_embedding_model_params ->> 'dimensions' IS NULL THEN
        RAISE EXCEPTION 'embedding_model_params must contain "dimensions" key';
    END IF;

    INSERT INTO dist_rag.vector_indexes (
        index_name, schema_name, index_options, index_creation_status,
        ai_provider, embedding_model_params
    )
    VALUES (
        v_mapping.registration_name, v_mapping.destination_schema, '{}'::jsonb,
        'NOT_STARTED'::dist_rag.index_build_status,
        r_ai_provider, r_embedding_model_params
    )
    RETURNING id INTO v_index_id;

    UPDATE dist_rag.column_embedding_registrations
    SET vector_index_id = v_index_id,
        status = 'ACTIVE',
        updated_at = NOW()
    WHERE id = r_mapping_id;

    RETURN v_index_id;
EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error initializing column embedding mapping "%": % - %', r_mapping_id, SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.init_column_embedding(UUID, dist_rag.ai_provider_enum, JSONB)
IS 'Step 2 of 2 (mirrors init_vector_index): supply the embedding-model config for a mapping created by create_column_embedding_mapping, create its dist_rag.vector_indexes config row, and activate it (status -> ACTIVE). Unlike init_vector_index, never creates a physical backing table -- the destination table already exists. Raises if the mapping is unknown or already initialized.';

CREATE OR REPLACE FUNCTION dist_rag.set_column_embedding_registration_status(
    r_registration_name TEXT,
    r_status            dist_rag.column_embedding_registration_status_enum
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = dist_rag, pg_catalog
AS $$
BEGIN
    UPDATE dist_rag.column_embedding_registrations
    SET status = r_status, updated_at = NOW()
    WHERE registration_name = r_registration_name;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'No registration named "%"', r_registration_name;
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error setting status for registration "%": % - %', r_registration_name, SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.set_column_embedding_registration_status(TEXT, dist_rag.column_embedding_registration_status_enum)
IS 'Pause or resume a column-embedding registration without deleting its configuration.';

CREATE OR REPLACE VIEW dist_rag.column_embedding_registration_stats AS
SELECT
    r.registration_name,
    r.status AS registration_status,
    r.destination_schema, r.destination_table,
    COUNT(p.dest_row_pk_in_text) FILTER (WHERE p.status = 'QUEUED')      AS pending_count,
    COUNT(p.dest_row_pk_in_text) FILTER (WHERE p.status = 'IN_PROGRESS') AS in_progress_count,
    COUNT(p.dest_row_pk_in_text) FILTER (WHERE p.status = 'FAILED')      AS failed_count,
    MAX(p.updated_at) AS last_activity_at
FROM dist_rag.column_embedding_registrations r
LEFT JOIN dist_rag.column_embedding_progress p ON p.registration_id = r.id
GROUP BY r.registration_name, r.status, r.destination_schema, r.destination_table;

COMMENT ON VIEW dist_rag.column_embedding_registration_stats
IS 'In-flight/failed progress-table counts per registration. Cannot show a "done" count -- successfully embedded rows are deleted from column_embedding_progress by design; use column_embedding_registration_progress(name) for total/embedded/pending counts against the actual destination table.';

CREATE OR REPLACE FUNCTION dist_rag.column_embedding_registration_progress(r_registration_name TEXT)
RETURNS TABLE(total_rows BIGINT, embedded_rows BIGINT, pending_or_failed_rows BIGINT)
LANGUAGE plpgsql AS $$
DECLARE
    v_reg RECORD;
BEGIN
    SELECT * INTO v_reg FROM dist_rag.column_embedding_registrations WHERE registration_name = r_registration_name;
    IF NOT FOUND THEN
        RAISE EXCEPTION 'No registration named "%"', r_registration_name;
    END IF;
    RETURN QUERY EXECUTE format(
        'SELECT COUNT(*), COUNT(*) FILTER (WHERE %I IS NOT NULL), COUNT(*) FILTER (WHERE %I IS NULL) FROM %I.%I',
        v_reg.destination_embedding_column, v_reg.destination_embedding_column,
        v_reg.destination_schema, v_reg.destination_table
    );
END;
$$;

COMMENT ON FUNCTION dist_rag.column_embedding_registration_progress(TEXT)
IS 'Total/embedded/pending-or-failed row counts for one registration''s actual destination table. Runs a full-table COUNT against that table -- fine every few minutes during a large backfill, not something to poll every few seconds.';


-- ============================================
-- PERMISSIONS
-- ============================================
-- 0.0.1's trailing GRANT ALL ON ALL TABLES/FUNCTIONS IN SCHEMA only covered
-- objects that existed at that point; re-grant explicitly for the new
-- objects this upgrade adds (no ALTER DEFAULT PRIVILEGES is in place).
--
-- column_embedding_registrations is SELECT-only: every write to it goes
-- through create_column_embedding_mapping / init_column_embedding /
-- set_column_embedding_registration_status, which validate the shape and
-- run SECURITY DEFINER. A wider grant here would let any role write or
-- repoint a registration directly, bypassing that validation and steering
-- the privileged worker at arbitrary tables. Same reasoning for the
-- read-only column_embedding_registration_stats view. column_embedding_progress
-- is different -- the worker itself claims/finalizes rows there directly
-- (registration_poller.py), with no SECURITY DEFINER function in between,
-- so it genuinely needs read/write for the role the worker connects as.
GRANT SELECT ON dist_rag.column_embedding_registrations TO PUBLIC;
GRANT ALL ON dist_rag.column_embedding_progress TO PUBLIC;
GRANT SELECT ON dist_rag.column_embedding_registration_stats TO PUBLIC;
GRANT EXECUTE ON FUNCTION dist_rag.create_column_embedding_mapping(
    TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT[], JSONB, TEXT, TEXT, TEXT, INTEGER, INTEGER, INTEGER, TEXT, JSONB, TEXT
) TO PUBLIC;
GRANT EXECUTE ON FUNCTION dist_rag.init_column_embedding(
    UUID, dist_rag.ai_provider_enum, JSONB
) TO PUBLIC;
GRANT EXECUTE ON FUNCTION dist_rag.set_column_embedding_registration_status(
    TEXT, dist_rag.column_embedding_registration_status_enum
) TO PUBLIC;
GRANT EXECUTE ON FUNCTION dist_rag.column_embedding_registration_progress(TEXT) TO PUBLIC;
