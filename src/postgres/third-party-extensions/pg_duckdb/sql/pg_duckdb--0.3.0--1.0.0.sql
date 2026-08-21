-- Move internal functions that are outside of the duckdb schema to the duckdb schema
ALTER FUNCTION duckdb_unresolved_type_operator(duckdb.unresolved_type, "any") SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator_bool(duckdb.unresolved_type, "any") SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator("any", duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator_bool("any", duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator(duckdb.unresolved_type, duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator_bool(duckdb.unresolved_type, duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_operator(duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any") SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any", another "any") SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_final(state duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_btree_cmp(duckdb.unresolved_type,duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER FUNCTION duckdb_unresolved_type_hash(duckdb.unresolved_type) SET SCHEMA duckdb;
ALTER OPERATOR CLASS duckdb_unresolved_type_ops USING btree SET SCHEMA duckdb;
ALTER OPERATOR CLASS duckdb_unresolved_type_hash_ops USING hash SET SCHEMA duckdb;

ALTER FUNCTION duckdb.duckdb_unresolved_type_operator(duckdb.unresolved_type, "any") RENAME TO unresolved_type_operator;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator_bool(duckdb.unresolved_type, "any") RENAME TO unresolved_type_operator_bool;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator("any", duckdb.unresolved_type) RENAME TO unresolved_type_operator;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator_bool("any", duckdb.unresolved_type) RENAME TO unresolved_type_operator_bool;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator(duckdb.unresolved_type, duckdb.unresolved_type) RENAME TO unresolved_type_operator;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator_bool(duckdb.unresolved_type, duckdb.unresolved_type) RENAME TO unresolved_type_operator_bool;
ALTER FUNCTION duckdb.duckdb_unresolved_type_operator(duckdb.unresolved_type) RENAME TO unresolved_type_operator;
ALTER FUNCTION duckdb.duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type) RENAME TO unresolved_type_state_trans;
ALTER FUNCTION duckdb.duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any") RENAME TO unresolved_type_state_trans;
ALTER FUNCTION duckdb.duckdb_unresolved_type_state_trans(state duckdb.unresolved_type, value duckdb.unresolved_type, other "any", another "any") RENAME TO unresolved_type_state_trans;
ALTER FUNCTION duckdb.duckdb_unresolved_type_final(state duckdb.unresolved_type) RENAME TO unresolved_type_final;
ALTER FUNCTION duckdb.duckdb_unresolved_type_btree_cmp(duckdb.unresolved_type,duckdb.unresolved_type) RENAME TO unresolved_type_btree_cmp;
ALTER FUNCTION duckdb.duckdb_unresolved_type_hash(duckdb.unresolved_type) RENAME TO unresolved_type_hash;
ALTER OPERATOR CLASS duckdb.duckdb_unresolved_type_ops USING btree RENAME TO unresolved_type_ops;
ALTER OPERATOR CLASS duckdb.duckdb_unresolved_type_hash_ops USING hash RENAME TO unresolved_type_hash_ops;

-- Ensure UTF8 encoding
DO $$
BEGIN
    SET LOCAL search_path = pg_catalog, pg_temp;
    IF current_setting('server_encoding') != 'UTF8' THEN
        RAISE EXCEPTION 'pg_duckdb can only be installed in a Postgres database with UTF8 encoding, this one is encoded using %.', current_setting('server_encoding');
    END IF;
END
$$;

-- Add "url_style" column to "secrets" table
ALTER TABLE duckdb.secrets ADD COLUMN url_style TEXT;

DROP FUNCTION duckdb.cache_delete;
DROP FUNCTION duckdb.cache_info;
DROP FUNCTION duckdb.cache;
DROP TYPE duckdb.cache_info;

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

-- Update JSON functions that return STRUCT to actually return the struct type.
-- To do so we need to drop + create them.
DROP FUNCTION @extschema@.json_transform("json" duckdb.json, structure duckdb.json);
DROP FUNCTION @extschema@.from_json("json" duckdb.json, structure duckdb.json);
DROP FUNCTION @extschema@.json_transform_strict("json" duckdb.json, structure duckdb.json);
DROP FUNCTION @extschema@.from_json_strict("json" duckdb.json, structure duckdb.json);

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

DROP FUNCTION duckdb.install_extension(TEXT);
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

CREATE FUNCTION pgduckdb_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME', 'pgduckdb_fdw_handler'
LANGUAGE C STRICT;

CREATE FUNCTION pgduckdb_fdw_validator(
    options text[],
    catalog oid
)
RETURNS void
AS 'MODULE_PATHNAME', 'pgduckdb_fdw_validator'
LANGUAGE C STRICT PARALLEL SAFE;

CREATE FOREIGN DATA WRAPPER duckdb
  HANDLER pgduckdb_fdw_handler
  VALIDATOR pgduckdb_fdw_validator;

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

-- Drop legacy secret objects
DROP SEQUENCE duckdb.secrets_table_seq;

-- CASCADE will drop the following triggers:
-- DROP TRIGGER duckdb_secret_r2_tr;
-- DROP TRIGGER secrets_table_seq_tr;
DROP TABLE duckdb.secrets CASCADE;

DROP FUNCTION duckdb.duckdb_secret_r2_check();
DROP FUNCTION duckdb.duckdb_update_secrets_table_seq();

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

ALTER TABLE duckdb.extensions ADD COLUMN repository TEXT NOT NULL DEFAULT 'core';
ALTER TABLE duckdb.extensions RENAME COLUMN enabled TO autoload;
ALTER TABLE duckdb.extensions ALTER COLUMN autoload SET NOT NULL;

CREATE FUNCTION duckdb.view(dbname text, schema text, view_name text, query text)
RETURNS SETOF duckdb.row
SET search_path = pg_catalog, pg_temp
AS 'MODULE_PATHNAME', 'duckdb_only_function'
LANGUAGE C;

ALTER FUNCTION duckdb.duckdb_alter_table_trigger RENAME TO _alter_table_trigger;
ALTER FUNCTION duckdb.duckdb_am_handler RENAME TO _am_handler;
ALTER FUNCTION duckdb.duckdb_create_table_trigger RENAME TO _create_table_trigger;
ALTER FUNCTION duckdb.duckdb_drop_trigger RENAME TO _drop_trigger;
ALTER FUNCTION duckdb.duckdb_grant_trigger RENAME TO _grant_trigger;
ALTER FUNCTION duckdb.duckdb_update_extensions_table_seq RENAME TO _update_extensions_table_seq;
