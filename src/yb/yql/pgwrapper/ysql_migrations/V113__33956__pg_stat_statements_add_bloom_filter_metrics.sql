-- Update pg_stat_statements to version 1.10-yb-2.2
DO $$
BEGIN
    -- The extension is absent in this database (a user may have dropped it);
    -- ::regclass would raise instead of skipping.
    IF to_regclass('pg_catalog.pg_stat_statements') IS NULL THEN
        RETURN;
    END IF;

    -- Checks for column docdb_bloom_filter_useful
    IF EXISTS (
        SELECT TRUE FROM pg_attribute
        WHERE attrelid = 'pg_catalog.pg_stat_statements'::regclass
            AND attname = 'docdb_bloom_filter_useful'
            AND NOT attisdropped
    ) THEN
        RETURN;
    END IF;

    -- V107's wrappers select from pg_catalog.pg_stat_statements, and the
    -- extension update must drop that view to change the function's OUT list.
    -- Drop the wrappers first, then let yb_create_global_view rebuild both
    -- against the new column list.
    DROP FOREIGN TABLE IF EXISTS pg_catalog."gv$pg_stat_statements";
    DROP VIEW IF EXISTS pg_catalog.pg_stat_statements_with_server_uuid;

    ALTER EXTENSION pg_stat_statements UPDATE TO '1.10-yb-2.2';

    IF EXISTS (
        SELECT 1 FROM pg_proc
        WHERE proname = 'yb_create_global_view'
          AND pronamespace = 'pg_catalog'::regnamespace
    ) THEN
        PERFORM pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_statements');
    END IF;
END $$;

-- The rebuilt wrappers have new OIDs, so re-record their grants as initial
-- privileges; otherwise pg_dump starts emitting them (V107 does the same for
-- the originals).
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  INSERT INTO pg_catalog.pg_init_privs (objoid, classoid, objsubid, initprivs, privtype)
      SELECT c.oid, 'pg_catalog.pg_class'::regclass, 0, c.relacl, 'i'
      FROM pg_catalog.pg_class c
      WHERE c.relnamespace = 'pg_catalog'::regnamespace
        AND c.relacl IS NOT NULL
        AND c.relname IN ('gv$pg_stat_statements',
                          'pg_stat_statements_with_server_uuid')
  ON CONFLICT DO NOTHING;
COMMIT;
