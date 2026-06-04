LOAD 'pg_duckdb';

-- We create a duckdb schema to store most of our things. We explicitly
-- don't use CREATE IF EXISTS or the schema key in the control file, so we know
-- for sure that the extension will own the schema and thus non superusers
-- cannot put random things in it, so we can assume it's safe. A few functions
-- we'll put into @extschema@ so that in normal usage they get put into the
-- public schema and are thus more easily usable. This is the case for the
-- read_csv, read_parquet and iceberg functions. It would be sad for usability
-- if people would have to prefix those with duckdb.read_csv
CREATE SCHEMA duckdb;
-- Allow users to see the objects in the duckdb schema. We'll manually revoke rights
-- for the dangerous ones.
GRANT USAGE ON SCHEMA duckdb to PUBLIC;

SET search_path = pg_catalog, pg_temp;

CREATE TYPE duckdb.iceberg_metadata_record AS (
  manifest_path TEXT,
  manifest_sequence_number NUMERIC,
  manifest_content  TEXT,
  status TEXT,
  content TEXT,
  file_path TEXT
);

CREATE FUNCTION @extschema@.iceberg_metadata(path text, allow_moved_paths BOOLEAN DEFAULT FALSE,
                                                       metadata_compression_codec TEXT DEFAULT 'none',
                                                       skip_schema_inference BOOLEAN DEFAULT FALSE,
                                                       version TEXT DEFAULT 'version-hint.text',
                                                       version_name_format TEXT DEFAULT 'v%s%s.metadata.json,%s%s.metadata.json')
RETURNS SETOF duckdb.iceberg_metadata_record LANGUAGE 'plpgsql'
SET search_path = pg_catalog, pg_temp
AS
$func$
BEGIN
    RAISE EXCEPTION 'Function `iceberg_metadata(TEXT)` only works with Duckdb execution.';
END;
$func$;

CREATE TYPE duckdb.iceberg_snapshots_record AS (
  sequence_number BIGINT,
  snapshot_id BIGINT,
  timestamp_ms TIMESTAMP,
  manifest_list TEXT
);

CREATE FUNCTION @extschema@.iceberg_snapshots(path text, metadata_compression_codec TEXT DEFAULT 'none',
                                                        skip_schema_inference BOOLEAN DEFAULT FALSE,
                                                        version TEXT DEFAULT 'version-hint.text',
                                                        version_name_format TEXT DEFAULT 'v%s%s.metadata.json,%s%s.metadata.json')
RETURNS SETOF duckdb.iceberg_snapshots_record LANGUAGE 'plpgsql'
SET search_path = pg_catalog, pg_temp
AS
$func$
BEGIN
    RAISE EXCEPTION 'Function `iceberg_snapshots(TEXT)` only works with Duckdb execution.';
END;
$func$;

-- If duckdb.postgres_role is configured let's make sure it's actually created.
-- If people change this setting after installing the extension they are
-- responsible for creating the user themselves. This is especially useful for
-- demo purposes and our pg_regress testing.
DO $$
DECLARE
    role_name text;
BEGIN
    SELECT current_setting('duckdb.postgres_role') INTO role_name;
    IF role_name != '' AND NOT EXISTS (
      SELECT FROM pg_catalog.pg_roles
      WHERE  rolname = role_name) THEN
        EXECUTE 'CREATE ROLE ' || quote_ident(current_setting('duckdb.postgres_role'));
    END IF;
END
$$;


SET search_path TO duckdb, pg_catalog, pg_temp;

-- Extensions

CREATE SEQUENCE duckdb.extensions_table_seq START WITH 1 INCREMENT BY 1;
SELECT pg_catalog.setval('duckdb.extensions_table_seq', 1);
GRANT SELECT ON duckdb.extensions_table_seq TO PUBLIC;

CREATE TABLE duckdb.extensions (
    name TEXT NOT NULL PRIMARY KEY,
    autoload BOOL DEFAULT TRUE NOT NULL,
    repository TEXT NOT NULL DEFAULT 'core'
);

CREATE FUNCTION duckdb._update_extensions_table_seq()
RETURNS TRIGGER
SET search_path = pg_catalog, pg_temp
AS
$$
BEGIN
    PERFORM nextval('duckdb.extensions_table_seq');
    RETURN NEW;
END;
$$ LANGUAGE PLpgSQL;
REVOKE ALL ON FUNCTION duckdb._update_extensions_table_seq() FROM PUBLIC;

CREATE TRIGGER extensions_table_seq_tr AFTER INSERT OR UPDATE OR DELETE ON extensions
EXECUTE FUNCTION duckdb._update_extensions_table_seq();

-- The following might seem unnecesasry, but it's needed to know if a dropped
-- table was a DuckDB table or not. See the comments and code in
-- duckdb_drop_table_trigger for details.
CREATE TABLE tables (
    relid regclass PRIMARY KEY,
    duckdb_db TEXT NOT NULL,
    motherduck_catalog_version TEXT,
    default_database TEXT NOT NULL
);

REVOKE ALL ON tables FROM PUBLIC;

CREATE FUNCTION raw_query(query TEXT) RETURNS void
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_raw_query';
REVOKE ALL ON FUNCTION raw_query(TEXT) FROM PUBLIC;

CREATE FUNCTION cache(object_path TEXT, type TEXT) RETURNS bool
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'cache';
REVOKE ALL ON FUNCTION cache(TEXT, TEXT) FROM PUBLIC;

CREATE FUNCTION duckdb._am_handler(internal)
    RETURNS table_am_handler
    SET search_path = pg_catalog, pg_temp
    AS 'MODULE_PATHNAME', 'duckdb_am_handler'
    LANGUAGE C;

CREATE ACCESS METHOD duckdb
    TYPE TABLE
    HANDLER duckdb._am_handler;

CREATE FUNCTION duckdb._drop_trigger() RETURNS event_trigger
    SET search_path = pg_catalog, pg_temp
    AS 'MODULE_PATHNAME', 'duckdb_drop_trigger' LANGUAGE C;

CREATE EVENT TRIGGER duckdb_drop_trigger ON sql_drop
    EXECUTE FUNCTION duckdb._drop_trigger();

CREATE FUNCTION duckdb._create_table_trigger() RETURNS event_trigger
    SET search_path = pg_catalog, pg_temp
    AS 'MODULE_PATHNAME', 'duckdb_create_table_trigger' LANGUAGE C;

CREATE EVENT TRIGGER duckdb_create_table_trigger ON ddl_command_end
    WHEN tag IN ('CREATE TABLE', 'CREATE TABLE AS')
    EXECUTE FUNCTION duckdb._create_table_trigger();

CREATE FUNCTION duckdb._alter_table_trigger() RETURNS event_trigger
    AS 'MODULE_PATHNAME', 'duckdb_alter_table_trigger' LANGUAGE C;

CREATE EVENT TRIGGER duckdb_alter_table_trigger ON ddl_command_end
    WHEN tag IN ('ALTER TABLE')
    EXECUTE FUNCTION duckdb._alter_table_trigger();

-- We explicitly don't set the search_path here in the function definition.
-- Because we actually need the original search_path that was active during the
-- GRANT to resolve the RangeVar using RangeVarGetRelid. We don't need this for
-- any of the other triggers since those don't manually resolve RangeVars, at
-- least not yet. So for those we might as well err on the side of caution and
-- force a safe search_path.
CREATE FUNCTION duckdb._grant_trigger() RETURNS event_trigger
    AS 'MODULE_PATHNAME', 'duckdb_grant_trigger' LANGUAGE C;

CREATE EVENT TRIGGER duckdb_grant_trigger ON ddl_command_end
    WHEN tag IN ('GRANT')
    EXECUTE FUNCTION duckdb._grant_trigger();

CREATE PROCEDURE force_motherduck_sync(drop_with_cascade BOOLEAN DEFAULT false)
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME';

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_catalog.pg_namespace WHERE nspname LIKE 'ddb$%') THEN
        RAISE 'pg_duckdb can only be installed if there are no schemas with a ddb$ prefix';
    END IF;
END
$$;

RESET search_path;

CREATE PROCEDURE duckdb.recycle_ddb()
    SET search_path = pg_catalog, pg_temp
    LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_recycle_ddb';
REVOKE ALL ON PROCEDURE duckdb.recycle_ddb() FROM PUBLIC;

CREATE FUNCTION @extschema@.approx_count_distinct_sfunc(bigint, anyelement)
RETURNS bigint
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE AGGREGATE @extschema@.approx_count_distinct(anyelement)
(
    sfunc = @extschema@.approx_count_distinct_sfunc,
    stype = bigint,
    initcond = 0
);

CREATE DOMAIN pg_catalog.blob AS bytea;
COMMENT ON DOMAIN pg_catalog.blob IS 'The DuckDB BLOB alias for BYTEA';

CREATE TYPE duckdb.row;
CREATE TYPE duckdb.unresolved_type;

-- TODO: Should we remove IMMUTABLE STRICT?
CREATE FUNCTION duckdb.row_in(cstring) RETURNS duckdb.row AS 'MODULE_PATHNAME', 'duckdb_row_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.row_out(duckdb.row) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_row_out' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.row_subscript(internal) RETURNS internal AS 'MODULE_PATHNAME', 'duckdb_row_subscript' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.row (
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.row_in,
    OUTPUT = duckdb.row_out,
    SUBSCRIPT = duckdb.row_subscript
);

CREATE FUNCTION duckdb.unresolved_type_in(cstring) RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_out(duckdb.unresolved_type) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_out' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_subscript(internal) RETURNS internal AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_subscript' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.unresolved_type (
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.unresolved_type_in,
    OUTPUT = duckdb.unresolved_type_out,
    SUBSCRIPT = duckdb.unresolved_type_subscript
);

-- Dummy functions for binary operators with unresolved type on the lefthand
CREATE FUNCTION duckdb.unresolved_type_operator(duckdb.unresolved_type, "any") RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_operator_bool(duckdb.unresolved_type, "any") RETURNS boolean AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Dummy functions for binary operators with unresolved type on the righthand
CREATE FUNCTION duckdb.unresolved_type_operator("any", duckdb.unresolved_type) RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_operator_bool("any", duckdb.unresolved_type) RETURNS boolean AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Dummy functions for binary operators with unresolved type on both sides
CREATE FUNCTION duckdb.unresolved_type_operator(duckdb.unresolved_type, duckdb.unresolved_type) RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_operator_bool(duckdb.unresolved_type, duckdb.unresolved_type) RETURNS boolean AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Dummy function for prefix/unary operators
CREATE FUNCTION duckdb.unresolved_type_operator(duckdb.unresolved_type) RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- prefix operators + and -
CREATE OPERATOR pg_catalog.+ (
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.- (
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

-- Basic comparison operators
CREATE OPERATOR pg_catalog.<= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.<= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.<= (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.< (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.< (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.< (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.<> (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.<> (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.<> (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.= (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.> (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.> (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.> (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.>= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.>= (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator_bool
);

CREATE OPERATOR pg_catalog.>= (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator_bool
);

-- binary math operators
CREATE OPERATOR pg_catalog.+ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.+ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.+ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.- (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.- (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.- (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.* (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog./ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog./ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog./ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

-- TODO: use other dummy function with better error
CREATE FUNCTION duckdb.unresolved_type_btree_cmp(duckdb.unresolved_type, duckdb.unresolved_type) RETURNS int AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Create a B-tree operator class for duckdb.unresolved_type, so it can be used in ORDER BY
CREATE OPERATOR CLASS duckdb.unresolved_type_ops
DEFAULT FOR TYPE duckdb.unresolved_type USING btree AS
    OPERATOR 1 < (duckdb.unresolved_type, duckdb.unresolved_type),
    OPERATOR 2 <= (duckdb.unresolved_type, duckdb.unresolved_type),
    OPERATOR 3 = (duckdb.unresolved_type, duckdb.unresolved_type),
    OPERATOR 4 >= (duckdb.unresolved_type, duckdb.unresolved_type),
    OPERATOR 5 > (duckdb.unresolved_type, duckdb.unresolved_type),
    FUNCTION 1 duckdb.unresolved_type_btree_cmp(duckdb.unresolved_type, duckdb.unresolved_type);

CREATE FUNCTION duckdb.unresolved_type_hash(duckdb.unresolved_type) RETURNS int AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Create a hash operator class for duckdb.unresolved_type, so it can be used in GROUP BY
CREATE OPERATOR CLASS duckdb_unresolved_type_hash_ops
DEFAULT FOR TYPE duckdb.unresolved_type USING hash AS
    OPERATOR 1 = (duckdb.unresolved_type, duckdb.unresolved_type),
    FUNCTION 1 duckdb.unresolved_type_hash(duckdb.unresolved_type);

-- TODO: create dedicated dummy C functions for these
--
-- State transition function
CREATE FUNCTION duckdb.unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type)
RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any")
RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any", another "any")
RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Final function
CREATE FUNCTION duckdb.unresolved_type_final(state duckdb.unresolved_type)
RETURNS duckdb.unresolved_type AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_operator' LANGUAGE C IMMUTABLE STRICT;

-- Aggregate functions

-- NOTE: any_value is already definied in core in PG16+, so we don't create it.
-- People using older Postgres versions can manually implement the aggregate if
-- they really require it.

CREATE AGGREGATE @extschema@.arbitrary(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_max(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_max(duckdb.unresolved_type, "any", "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_max_null(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_min(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_min(duckdb.unresolved_type, "any", "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.arg_min_null(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.array_agg(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.avg(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bit_and(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bit_or(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bit_xor(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bitstring_agg(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bool_and(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.bool_or(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

-- NOTE: count(*) and count(duckdb.unresolved_type) are already defined in the core

CREATE AGGREGATE @extschema@.favg(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.first(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.fsum(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.geomean(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.histogram(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.histogram(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.histogram_exact(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.last(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.list(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.max(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.max(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.max_by(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.max_by(duckdb.unresolved_type, "any", "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.min(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.min_by(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.min_by(duckdb.unresolved_type, "any", "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.product(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.string_agg(duckdb.unresolved_type, "any") (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE AGGREGATE @extschema@.sum(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);


-- "AS ASSIGNMENT" cast to boolean for unresolved types, so that they can be
-- used as the final expression in a WHERE clause
CREATE CAST (duckdb.unresolved_type AS boolean)
    WITH INOUT
    AS ASSIGNMENT;

-- Regular casts for all our supported types
-- BOOLEAN (skiping plain boolean because it's right above)
CREATE CAST (duckdb.unresolved_type AS boolean[])
    WITH INOUT;

-- TINYINT (CHAR)
CREATE CAST (duckdb.unresolved_type AS char)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS char[])
    WITH INOUT;

-- SMALLINT (INT2)
CREATE CAST (duckdb.unresolved_type AS smallint)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS smallint[])
    WITH INOUT;

-- INTEGER (INT4)
CREATE CAST (duckdb.unresolved_type AS integer)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS integer[])
    WITH INOUT;

-- BIGINT (INT8)
CREATE CAST (duckdb.unresolved_type AS bigint)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS bigint[])
    WITH INOUT;

-- VARCHAR
CREATE CAST (duckdb.unresolved_type AS varchar)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS varchar[])
    WITH INOUT;

-- DATE
CREATE CAST (duckdb.unresolved_type AS date)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS date[])
    WITH INOUT;

-- TIMESTAMP
CREATE CAST (duckdb.unresolved_type AS timestamp)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS timestamp[])
    WITH INOUT;

-- TIMESTAMP WITH TIME ZONE
CREATE CAST (duckdb.unresolved_type AS timestamptz)
    WITH INOUT;

-- FLOAT
CREATE CAST (duckdb.unresolved_type AS real)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS real[])
    WITH INOUT;

-- DOUBLE
CREATE CAST (duckdb.unresolved_type AS double precision)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS double precision[])
    WITH INOUT;

-- NUMERIC (DECIMAL)
CREATE CAST (duckdb.unresolved_type AS numeric)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS numeric[])
    WITH INOUT;

-- UUID
CREATE CAST (duckdb.unresolved_type AS uuid)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS uuid[])
    WITH INOUT;

-- JSON
CREATE CAST (duckdb.unresolved_type AS json)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS json[])
    WITH INOUT;

-- JSONB
CREATE CAST (duckdb.unresolved_type AS jsonb)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS jsonb[])
    WITH INOUT;

-- read_parquet function for single path
CREATE FUNCTION @extschema@.read_parquet(path text, binary_as_string BOOLEAN DEFAULT FALSE,
                                                   filename BOOLEAN DEFAULT FALSE,
                                                   file_row_number BOOLEAN DEFAULT FALSE,
                                                   hive_partitioning BOOLEAN DEFAULT FALSE,
                                                   union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- read_parquet function for array of paths
CREATE FUNCTION @extschema@.read_parquet(path text[], binary_as_string BOOLEAN DEFAULT FALSE,
                                                     filename BOOLEAN DEFAULT FALSE,
                                                     file_row_number BOOLEAN DEFAULT FALSE,
                                                     hive_partitioning BOOLEAN DEFAULT FALSE,
                                                     union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- read_csv function for single path
CREATE FUNCTION @extschema@.read_csv(path text, all_varchar BOOLEAN DEFAULT FALSE,
                                               allow_quoted_nulls BOOLEAN DEFAULT TRUE,
                                               auto_detect BOOLEAN DEFAULT TRUE,
                                               auto_type_candidates TEXT[] DEFAULT ARRAY[]::TEXT[],
                                               compression VARCHAR DEFAULT 'auto',
                                               dateformat VARCHAR DEFAULT '',
                                               decimal_separator VARCHAR DEFAULT '.',
                                               delim VARCHAR DEFAULT ',',
                                               escape VARCHAR DEFAULT '"',
                                               filename BOOLEAN DEFAULT FALSE,
                                               force_not_null TEXT[] DEFAULT ARRAY[]::TEXT[],
                                               header BOOLEAN DEFAULT FALSE,
                                               hive_partitioning BOOLEAN DEFAULT FALSE,
                                               ignore_errors BOOLEAN DEFAULT FALSE,
                                               max_line_size BIGINT DEFAULT 2097152,
                                               names TEXT[] DEFAULT ARRAY[]::TEXT[],
                                               new_line VARCHAR DEFAULT '',
                                               normalize_names BOOLEAN DEFAULT FALSE,
                                               null_padding BOOLEAN DEFAULT FALSE,
                                               nullstr TEXT[] DEFAULT ARRAY[]::TEXT[],
                                               parallel BOOLEAN DEFAULT FALSE,
                                               quote VARCHAR DEFAULT '"',
                                               sample_size BIGINT DEFAULT 20480,
                                               sep VARCHAR DEFAULT ',',
                                               skip BIGINT DEFAULT 0,
                                               timestampformat VARCHAR DEFAULT '',
                                               types TEXT[] DEFAULT ARRAY[]::TEXT[],
                                               union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- read_csv function for array of paths
CREATE FUNCTION @extschema@.read_csv(path text[], all_varchar BOOLEAN DEFAULT FALSE,
                                                  allow_quoted_nulls BOOLEAN DEFAULT TRUE,
                                                  auto_detect BOOLEAN DEFAULT TRUE,
                                                  auto_type_candidates TEXT[] DEFAULT ARRAY[]::TEXT[],
                                                  compression VARCHAR DEFAULT 'auto',
                                                  dateformat VARCHAR DEFAULT '',
                                                  decimal_separator VARCHAR DEFAULT '.',
                                                  delim VARCHAR DEFAULT ',',
                                                  escape VARCHAR DEFAULT '"',
                                                  filename BOOLEAN DEFAULT FALSE,
                                                  force_not_null TEXT[] DEFAULT ARRAY[]::TEXT[],
                                                  header BOOLEAN DEFAULT FALSE,
                                                  hive_partitioning BOOLEAN DEFAULT FALSE,
                                                  ignore_errors BOOLEAN DEFAULT FALSE,
                                                  max_line_size BIGINT DEFAULT 2097152,
                                                  names TEXT[] DEFAULT ARRAY[]::TEXT[],
                                                  new_line VARCHAR DEFAULT '',
                                                  normalize_names BOOLEAN DEFAULT FALSE,
                                                  null_padding BOOLEAN DEFAULT FALSE,
                                                  nullstr TEXT[] DEFAULT ARRAY[]::TEXT[],
                                                  parallel BOOLEAN DEFAULT FALSE,
                                                  quote VARCHAR DEFAULT '"',
                                                  sample_size BIGINT DEFAULT 20480,
                                                  sep VARCHAR DEFAULT ',',
                                                  skip BIGINT DEFAULT 0,
                                                  timestampformat VARCHAR DEFAULT '',
                                                  types TEXT[] DEFAULT ARRAY[]::TEXT[],
                                                  union_by_name BOOLEAN DEFAULT FALSE)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- iceberg_scan function
CREATE FUNCTION @extschema@.iceberg_scan(path text, allow_moved_paths BOOLEAN DEFAULT FALSE,
                                                   mode TEXT DEFAULT '',
                                                   metadata_compression_codec TEXT DEFAULT 'none',
                                                   skip_schema_inference BOOLEAN DEFAULT FALSE,
                                                   version TEXT DEFAULT 'version-hint.text',
                                                   version_name_format TEXT DEFAULT 'v%s%s.metadata.json,%s%s.metadata.json')
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- delta_scan function
CREATE FUNCTION @extschema@.delta_scan(path text)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- read_json function for single path
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
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- read_json function for array of paths
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
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.query(query text)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE TYPE duckdb.json;
COMMENT ON TYPE duckdb.json IS 'A helper type that allows passing JSON, JSONB, duckdb.unresolved_type and string literals to DuckDB its json related functions';
CREATE FUNCTION duckdb.json_in(cstring) RETURNS duckdb.json AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.json_out(duckdb.json) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_out' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.json_subscript(internal) RETURNS internal AS 'MODULE_PATHNAME', 'duckdb_unresolved_type_subscript' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.json (
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.json_in,
    OUTPUT = duckdb.json_out,
    SUBSCRIPT = duckdb.json_subscript
);

CREATE CAST (duckdb.unresolved_type AS duckdb.json)
    WITH INOUT AS IMPLICIT;

CREATE CAST (json AS duckdb.json)
    WITH INOUT AS IMPLICIT;

CREATE CAST (jsonb AS duckdb.json)
    WITH INOUT AS IMPLICIT;

-- json_exists
CREATE FUNCTION @extschema@.json_exists("json" duckdb.json, path VARCHAR)
RETURNS boolean
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_extract
CREATE FUNCTION @extschema@.json_extract("json" duckdb.json, path bigint)
RETURNS JSON
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_extract("json" duckdb.json, path VARCHAR)
RETURNS JSON
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_extract with path list
CREATE FUNCTION @extschema@.json_extract("json" duckdb.json, path VARCHAR[])
RETURNS JSON[]
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_extract_string
CREATE FUNCTION @extschema@.json_extract_string("json" duckdb.json, path bigint)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_extract_string("json" duckdb.json, path VARCHAR)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_extract_string
CREATE FUNCTION @extschema@.json_extract_string("json" duckdb.json, path VARCHAR[])
RETURNS VARCHAR[]
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_value
CREATE FUNCTION @extschema@.json_value("json" duckdb.json, path bigint)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_value("json" duckdb.json, path VARCHAR)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_value("json" duckdb.json, path VARCHAR[])
RETURNS VARCHAR[]
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_array_length
CREATE FUNCTION @extschema@.json_array_length("json" duckdb.json, path_input VARCHAR DEFAULT NULL)
RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_array_length("json" duckdb.json, path_input VARCHAR[])
RETURNS bigint[]
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_contains
CREATE FUNCTION @extschema@.json_contains(json_haystack duckdb.json, json_needle duckdb.json)
RETURNS boolean
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_keys
CREATE FUNCTION @extschema@.json_keys("json" duckdb.json, path VARCHAR DEFAULT NULL)
RETURNS SETOF VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_keys("json" duckdb.json, path VARCHAR[])
RETURNS SETOF VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_structure
CREATE FUNCTION @extschema@.json_structure("json" duckdb.json)
RETURNS JSON
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;
-- json_type
CREATE FUNCTION @extschema@.json_type("json" duckdb.json, path VARCHAR DEFAULT NULL)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.json_type("json" duckdb.json, path VARCHAR[])
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_valid
CREATE FUNCTION @extschema@.json_valid("json" duckdb.json)
RETURNS boolean
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json
CREATE FUNCTION @extschema@.json("json" duckdb.json)
RETURNS VARCHAR
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_group_array
CREATE FUNCTION @extschema@.json_group_array_sfunc(JSON, "any")
RETURNS JSON
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE AGGREGATE @extschema@.json_group_array("any")
(
    sfunc = @extschema@.json_group_array_sfunc,
    stype = JSON,
    initcond = 0
);

-- json_group_object
CREATE FUNCTION @extschema@.json_group_object_sfunc(JSON, "any", "any")
RETURNS JSON
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE AGGREGATE @extschema@.json_group_object("any", "any")
(
    sfunc = @extschema@.json_group_object_sfunc,
    stype = JSON,
    initcond = 0
);

-- json_group_structure
CREATE FUNCTION @extschema@.json_group_structure_sfunc(JSON, duckdb.json)
RETURNS JSON
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE AGGREGATE @extschema@.json_group_structure(duckdb.json)
(
    sfunc = @extschema@.json_group_structure_sfunc,
    stype = JSON,
    initcond = 0
);

GRANT ALL ON FUNCTION duckdb.raw_query(TEXT) TO PUBLIC;
GRANT ALL ON PROCEDURE duckdb.recycle_ddb() TO PUBLIC;

-- Ensure UTF8 encoding
DO $$
BEGIN
    SET LOCAL search_path = pg_catalog, pg_temp;
    IF current_setting('server_encoding') != 'UTF8' THEN
        RAISE EXCEPTION 'pg_duckdb can only be installed in a Postgres database with UTF8 encoding, this one is encoded using %.', current_setting('server_encoding');
    END IF;
END
$$;

-- New Data type to handle duckdb struct
CREATE TYPE duckdb.struct;
CREATE FUNCTION duckdb.struct_in(cstring) RETURNS duckdb.struct AS 'MODULE_PATHNAME', 'duckdb_struct_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.struct_out(duckdb.struct) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_struct_out' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.struct_subscript(internal) RETURNS internal AS 'MODULE_PATHNAME', 'duckdb_struct_subscript' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.struct (
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.struct_in,
    OUTPUT = duckdb.struct_out,
    SUBSCRIPT = duckdb.struct_subscript
);

-- json_transform
CREATE FUNCTION @extschema@.json_transform("json" duckdb.json, structure duckdb.json)
RETURNS duckdb.struct
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- from_json
CREATE FUNCTION @extschema@.from_json("json" duckdb.json, structure duckdb.json)
RETURNS duckdb.struct
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- json_transform_strict
CREATE FUNCTION @extschema@.json_transform_strict("json" duckdb.json, structure duckdb.json)
RETURNS duckdb.struct
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- from_json_strict
CREATE FUNCTION @extschema@.from_json_strict("json" duckdb.json, structure duckdb.json)
RETURNS duckdb.struct
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.install_extension(extension_name TEXT, source TEXT DEFAULT 'core') RETURNS void
    SECURITY DEFINER
    SET search_path = pg_catalog, pg_temp
    SET duckdb.force_execution = false
    LANGUAGE C AS 'MODULE_PATHNAME', 'install_extension';
REVOKE ALL ON FUNCTION duckdb.install_extension(TEXT, TEXT) FROM PUBLIC;

CREATE FUNCTION duckdb.autoload_extension(extension_name TEXT, autoload BOOLEAN DEFAULT TRUE) RETURNS void
    SECURITY DEFINER
    SET search_path = pg_catalog, pg_temp
    SET duckdb.force_execution = false
    LANGUAGE C AS 'MODULE_PATHNAME', 'duckdb_autoload_extension';
REVOKE ALL ON FUNCTION duckdb.autoload_extension(TEXT, BOOLEAN) FROM PUBLIC;

CREATE FUNCTION duckdb.load_extension(extension_name TEXT) RETURNS void
    SET search_path = pg_catalog, pg_temp
    SET duckdb.force_execution = false
    LANGUAGE C AS 'MODULE_PATHNAME', 'duckdb_load_extension';
-- load_extension is allowed for all users, because it is trivial for users run
-- a raw query that does the same. This function only exists for completeness
-- and convenience.

-- The min aggregate was somehow missing from the list of aggregates in 0.3.0
CREATE AGGREGATE @extschema@.min(duckdb.unresolved_type) (
    SFUNC = duckdb.unresolved_type_state_trans,
    STYPE = duckdb.unresolved_type,
    FINALFUNC = duckdb.unresolved_type_final
);

CREATE FUNCTION @extschema@.strftime(date, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strftime(timestamp, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strftime(timestamptz, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strftime(duckdb.unresolved_type, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strptime(text, text) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strptime(duckdb.unresolved_type, text) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strptime(text, text[]) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.strptime(duckdb.unresolved_type, text[]) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(interval) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(date) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(timestamp) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(timestamptz) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(time) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(timetz) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch(duckdb.unresolved_type) RETURNS double
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(interval) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(date) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(timestamp) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(timestamptz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(time) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(timetz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(bigint) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ms(duckdb.unresolved_type) RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(interval) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(date) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(timestamp) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(timestamptz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(time) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(timetz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_us(duckdb.unresolved_type) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(interval) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(date) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(timestamp) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(timestamptz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(time) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(timetz) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.epoch_ns(duckdb.unresolved_type) RETURNS bigint
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.make_timestamp(microseconds bigint) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.make_timestamp(microseconds duckdb.unresolved_type) RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.make_timestamptz(microseconds bigint) RETURNS timestamptz
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.make_timestamptz(microseconds duckdb.unresolved_type) RETURNS timestamptz
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.date_trunc(text, duckdb.unresolved_type) RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.length(duckdb.unresolved_type) RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.regexp_replace(duckdb.unresolved_type, text, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.regexp_replace(duckdb.unresolved_type, text, text, text) RETURNS text
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

-- time_bucket is a common name for a function, specifically the Timescale also
-- has a function with the same name and arguments. If any of these functions
-- conflicts with an already existing function, we make sure not to create any
-- of them for consistency. We'll always create the time_bucket function in the
-- duckdb schema, that people can use even if they have timescale installed
-- too. We'll also always create the unresolved_type time_bucket variants in
-- @extschema@, because these will never conflict.
DO $$
BEGIN
CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts date)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts date, origin date)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts date, time_offset interval)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp, time_offset interval)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp, origin timestamp)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp with time zone)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp with time zone, time_offset interval)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp with time zone, origin timestamp with time zone)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts timestamp with time zone, timezone varchar)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

EXCEPTION
WHEN duplicate_function THEN
    -- Do nothing, it's probably the timescale variant. We still have
    -- duckdb.time_bucket and the unresolved_type versions as a backup for
    -- users. We do let the user know that we skipped the creation of the
    -- function though.
    RAISE WARNING 'time_bucket function already exists, use duckdb.time_bucket instead. This is usually because the timescale extension is installed.';
END;
$$;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type, time_offset interval)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin date)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin timestamp)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin timestamp with time zone)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.time_bucket(bucket_width interval, ts duckdb.unresolved_type, timezone varchar)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts date)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts date, origin date)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts date, time_offset interval)
RETURNS date
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp, time_offset interval)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp, origin timestamp)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp with time zone)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp with time zone, time_offset interval)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp with time zone, origin timestamp with time zone)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts timestamp with time zone, timezone varchar)
RETURNS timestamp with time zone
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type, time_offset interval)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin date)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin timestamp)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type, origin timestamp with time zone)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.time_bucket(bucket_width interval, ts duckdb.unresolved_type, timezone varchar)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE CAST (duckdb.unresolved_type AS interval)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS interval[])
    WITH INOUT;

CREATE CAST (duckdb.unresolved_type AS time)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS time[])
    WITH INOUT;

CREATE CAST (duckdb.unresolved_type AS timetz)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS timetz[])
    WITH INOUT;

CREATE CAST (duckdb.unresolved_type AS bit)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS bit[])
    WITH INOUT;

CREATE CAST (duckdb.unresolved_type AS bytea)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS bytea[])
    WITH INOUT;

CREATE CAST (duckdb.unresolved_type AS text)
    WITH INOUT;
CREATE CAST (duckdb.unresolved_type AS text[])
    WITH INOUT;

CREATE OPERATOR pg_catalog.~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.~~* (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~ (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~ (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~* (
    LEFTARG = duckdb.unresolved_type,
    RIGHTARG = "any",
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE OPERATOR pg_catalog.!~~* (
    LEFTARG = "any",
    RIGHTARG = duckdb.unresolved_type,
    FUNCTION = duckdb.unresolved_type_operator
);

CREATE TYPE duckdb.union;
CREATE FUNCTION duckdb.union_in(cstring) RETURNS duckdb.union AS 'MODULE_PATHNAME', 'duckdb_union_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.union_out(duckdb.union) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_union_out' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.union(
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.union_in,
    OUTPUT = duckdb.union_out
);

CREATE FUNCTION @extschema@.union_extract(union_col duckdb.unresolved_type, tag text)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.union_extract(union_col duckdb.union, tag text)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.union_tag(union_col duckdb.unresolved_type)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION @extschema@.union_tag(union_col duckdb.union)
RETURNS duckdb.unresolved_type
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

CREATE FUNCTION duckdb.is_motherduck_enabled()
RETURNS bool
SET search_path = pg_catalog, pg_temp
LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_is_motherduck_enabled';
REVOKE ALL ON FUNCTION duckdb.is_motherduck_enabled() FROM PUBLIC;

CREATE FUNCTION duckdb.fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME', 'pgduckdb_fdw_handler'
LANGUAGE C STRICT;

CREATE FUNCTION duckdb.fdw_validator(
    options text[],
    catalog oid
)
RETURNS void
AS 'MODULE_PATHNAME', 'pgduckdb_fdw_validator'
LANGUAGE C STRICT PARALLEL SAFE;

CREATE FOREIGN DATA WRAPPER duckdb
  HANDLER duckdb.fdw_handler
  VALIDATOR duckdb.fdw_validator;

CREATE PROCEDURE duckdb.enable_motherduck(TEXT DEFAULT '::FROM_ENV::', TEXT DEFAULT '')
LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_enable_motherduck';

CREATE TYPE duckdb.map;
CREATE FUNCTION duckdb.map_in(cstring) RETURNS duckdb.map AS 'MODULE_PATHNAME', 'duckdb_map_in' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.map_out(duckdb.map) RETURNS cstring AS 'MODULE_PATHNAME', 'duckdb_map_out' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION duckdb.map_subscript(internal) RETURNS internal AS 'MODULE_PATHNAME', 'duckdb_map_subscript' LANGUAGE C IMMUTABLE STRICT;
CREATE TYPE duckdb.map(
    INTERNALLENGTH = VARIABLE,
    INPUT = duckdb.map_in,
    OUTPUT = duckdb.map_out,
    SUBSCRIPT = duckdb.map_subscript
);

-- Secrets helpers
CREATE FUNCTION duckdb.create_simple_secret(
    type          TEXT, -- One of (S3, GCS, R2)
    key_id        TEXT,
    secret        TEXT,
    session_token TEXT DEFAULT '',
    region        TEXT DEFAULT '',
    url_style     TEXT DEFAULT '',
    provider      TEXT DEFAULT '',
    endpoint      TEXT DEFAULT '',
    scope         TEXT DEFAULT ''
)
RETURNS TEXT
SET search_path = pg_catalog, pg_temp
LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_create_simple_secret';

CREATE FUNCTION duckdb.create_azure_secret(connection_string TEXT, scope TEXT DEFAULT '')
RETURNS TEXT
SET search_path = pg_catalog, pg_temp
LANGUAGE C AS 'MODULE_PATHNAME', 'pgduckdb_create_azure_secret';

CREATE FUNCTION duckdb.view(dbname text, schema text, view_name text, query text)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;
