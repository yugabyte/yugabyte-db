CREATE FUNCTION @extschema@.delta_scan(path text)
RETURNS SETOF record LANGUAGE 'plpgsql'
SET search_path = pg_catalog, pg_temp
AS
$func$
BEGIN
    RAISE EXCEPTION 'Function `delta_scan(TEXT)` only works with Duckdb execution.';
END;
$func$;

CREATE FUNCTION @extschema@.read_json(path text, auto_detect BOOLEAN DEFAULT FALSE,
                                                 compression VARCHAR DEFAULT 'auto',
                                                 dateformat VARCHAR DEFAULT 'iso',
                                                 format VARCHAR DEFAULT 'array',
                                                 ignore_errors BOOLEAN DEFAULT FALSE,
                                                 maximum_depth BIGINT DEFAULT -1,
                                                 maximum_object_size INT DEFAULT 16777216,
                                                 records VARCHAR DEFAULT 'records',
                                                 sample_size BIGINT DEFAULT 20480,
                                                 timestampformat VARCHAR DEFAULT 'iso',
                                                 union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF record LANGUAGE 'plpgsql'
SET search_path = pg_catalog, pg_temp
AS
$func$
BEGIN
    RAISE EXCEPTION 'Function `read_json(TEXT)` only works with Duckdb execution.';
END;
$func$;

CREATE FUNCTION @extschema@.read_json(path text[], auto_detect BOOLEAN DEFAULT FALSE,
                                                   compression VARCHAR DEFAULT 'auto',
                                                   dateformat VARCHAR DEFAULT 'iso',
                                                   format VARCHAR DEFAULT 'array',
                                                   ignore_errors BOOLEAN DEFAULT FALSE,
                                                   maximum_depth BIGINT DEFAULT -1,
                                                   maximum_object_size INT DEFAULT 16777216,
                                                   records VARCHAR DEFAULT 'records',
                                                   sample_size BIGINT DEFAULT 20480,
                                                   timestampformat VARCHAR DEFAULT 'iso',
                                                   union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF record LANGUAGE 'plpgsql'
SET search_path = pg_catalog, pg_temp
AS
$func$
BEGIN
    RAISE EXCEPTION 'Function `read_json(TEXT[])` only works with Duckdb execution.';
END;
$func$;

CREATE TYPE duckdb.cache_info AS (
  remote_path TEXT,
  cache_key TEXT,
  cache_file_size BIGINT,
  cache_file_timestamp TIMESTAMPTZ
);

CREATE FUNCTION duckdb.cache_info()
    RETURNS SETOF duckdb.cache_info
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'cache_info';
REVOKE ALL ON FUNCTION duckdb.cache_info() FROM PUBLIC;

CREATE FUNCTION duckdb.cache_delete(cache_key TEXT)
    RETURNS bool
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'cache_delete';
REVOKE ALL ON FUNCTION duckdb.cache_delete(cache_key TEXT) FROM PUBLIC;

DROP FUNCTION duckdb.recycle_ddb();
CREATE PROCEDURE duckdb.recycle_ddb()
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_recycle_ddb';
REVOKE ALL ON PROCEDURE duckdb.recycle_ddb() FROM PUBLIC;

ALTER TABLE duckdb.secrets ADD COLUMN scope TEXT;

ALTER TABLE duckdb.tables ADD COLUMN default_database TEXT NOT NULL DEFAULT 'my_db';
ALTER TABLE duckdb.tables ALTER COLUMN default_database DROP DEFAULT;

-- Alter duckdb.secrets to allow column "key_id" & "secret" to be NULL
ALTER TABLE duckdb.secrets ALTER COLUMN key_id DROP NOT NULL;
ALTER TABLE duckdb.secrets ALTER COLUMN secret DROP NOT NULL;

-- Update "type_constraint" CHECK on "type" to allow "Azure"
ALTER TABLE duckdb.secrets DROP CONSTRAINT type_constraint;
ALTER TABLE duckdb.secrets ADD CONSTRAINT type_constraint CHECK (upper(type) IN ('S3', 'GCS', 'R2', 'AZURE'));

-- Add "azure_connection_string" column
ALTER TABLE duckdb.secrets ADD COLUMN connection_string TEXT;
