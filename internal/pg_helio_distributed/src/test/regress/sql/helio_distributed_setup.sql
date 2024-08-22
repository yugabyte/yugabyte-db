CREATE SCHEMA IF NOT EXISTS helio_distributed_test_helpers;

SELECT datname, datcollate, datctype, pg_encoding_to_char(encoding), datlocprovider FROM pg_database;


CREATE OR REPLACE FUNCTION helio_distributed_test_helpers.latest_helio_distributed_version()
  RETURNS text
  LANGUAGE plpgsql
AS $fn$
DECLARE
  v_latest_version text;
BEGIN
  WITH cte AS (SELECT version from pg_available_extension_versions WHERE name='pg_helio_distributed'),
      cte2 AS (SELECT r[1]::integer as r1, r[2]::integer as r2, r[3]::integer as r3, COALESCE(r[4]::integer,0) as r4, version
      FROM cte, regexp_matches(version,'([0-9]+)\.([0-9]+)-([0-9]+)\.?([0-9]+)?','') r ORDER BY r1 DESC, r2 DESC, r3 DESC, r4 DESC LIMIT 1)
      SELECT version INTO v_latest_version FROM cte2;
  
  RETURN v_latest_version;
END;
$fn$;

CREATE OR REPLACE FUNCTION helio_distributed_test_helpers.create_latest_extension(p_cascade bool default false)
  RETURNS void
  LANGUAGE plpgsql
AS $fn$
DECLARE
  v_latest_version text;
BEGIN
  SELECT helio_distributed_test_helpers.latest_helio_distributed_version() INTO v_latest_version;

  IF p_cascade THEN
    EXECUTE format($$CREATE EXTENSION pg_helio_distributed WITH VERSION '%1$s' CASCADE$$, v_latest_version);
  ELSE
    EXECUTE format($$CREATE EXTENSION pg_helio_distributed WITH VERSION '%1$s'$$, v_latest_version);
  END IF;

  CREATE TABLE IF NOT EXISTS helio_data.changes (
  /* Catalog ID of the collection to which this change belongs to */
    collection_id bigint not null,
    /* derived shard key field of the document that changed */
    shard_key_value bigint not null,
    /* object ID of the document that was changed */
    object_id helio_core.bson not null,
    PRIMARY KEY(shard_key_value, object_id)
  );
END;
$fn$;

CREATE OR REPLACE FUNCTION helio_distributed_test_helpers.upgrade_extension(target_version text)
RETURNS void AS $$
DECLARE
  ran_upgrade_script bool;
BEGIN
  IF target_version IS NULL THEN
    SELECT helio_distributed_test_helpers.latest_helio_distributed_version() INTO target_version;
  END IF;

  SET citus.enable_ddl_propagation = off;
  EXECUTE format($cmd$ ALTER EXTENSION pg_helio_distributed UPDATE to %L $cmd$, target_version);
  EXECUTE format($cmd$ ALTER EXTENSION pg_helio_api UPDATE to %L $cmd$, target_version);
  EXECUTE format($cmd$ ALTER EXTENSION pg_helio_core UPDATE to %L $cmd$, target_version);

  IF target_version = '1.0-4.1' THEN
    SET client_min_messages TO WARNING;
      PERFORM helio_api_distributed.complete_upgrade();
    SET client_min_messages TO DEFAULT;
  END IF;

  IF target_version IS NULL OR target_version > '1.0-4.1' THEN
    SET client_min_messages TO WARNING;
    SELECT helio_api_distributed.complete_upgrade() INTO ran_upgrade_script;
    SET client_min_messages TO DEFAULT;

    RAISE NOTICE 'Ran Upgrade Script: %', ran_upgrade_script;
  END IF;
END;
$$ language plpgsql;

-- The schema version should NOT match the binary version
SELECT extversion FROM pg_extension WHERE extname = 'pg_helio_distributed' \gset

-- Check if recreating the extension works
DROP EXTENSION IF EXISTS pg_helio_distributed CASCADE;
DROP EXTENSION IF EXISTS pg_helio_api CASCADE;
DROP EXTENSION IF EXISTS pg_helio_core CASCADE;

-- Install the latest available pg_helio_distributed version
SELECT helio_distributed_test_helpers.create_latest_extension(p_cascade => TRUE);

-- The schema version now should match the binary version
SELECT extversion FROM pg_extension WHERE extname = 'pg_helio_distributed' \gset

SELECT helio_api_distributed.initialize_cluster();

-- Call initialize again (just to ensure idempotence)
SELECT helio_api_distributed.initialize_cluster();
GRANT helio_admin_role TO current_user;
