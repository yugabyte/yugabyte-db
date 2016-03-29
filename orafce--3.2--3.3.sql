ALTER FUNCTION dbms_assert.enquote_name ( character varying ) STRICT;
ALTER FUNCTION dbms_assert.enquote_name ( character varying, boolean ) STRICT;
ALTER FUNCTION dbms_assert.noop ( character varying ) STRICT;

CREATE FUNCTION pg_catalog.trunc(value timestamp without time zone, fmt text)
RETURNS timestamp without time zone
AS 'MODULE_PATHNAME', 'ora_timestamp_trunc'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(timestamp without time zone, text) IS 'truncate date according to the specified format';

CREATE FUNCTION pg_catalog.round(value timestamp without time zone, fmt text)
RETURNS timestamp without time zone
AS 'MODULE_PATHNAME','ora_timestamp_round'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(timestamp with time zone, text) IS 'round dates according to the specified format';

CREATE FUNCTION pg_catalog.round(value timestamp without time zone)
RETURNS timestamp without time zone
AS $$ SELECT pg_catalog.round($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(timestamp without time zone) IS 'will round dates according to the specified format';

CREATE FUNCTION pg_catalog.trunc(value timestamp without time zone)
RETURNS timestamp without time zone
AS $$ SELECT pg_catalog.trunc($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(timestamp without time zone) IS 'truncate date according to the specified format';
