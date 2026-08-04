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

CREATE FUNCTION plvdate.use_great_friday(bool)
RETURNS void
AS 'MODULE_PATHNAME','plvdate_use_great_friday'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.use_great_friday(bool) IS 'Great Friday will be holiday';

CREATE FUNCTION plvdate.use_great_friday()
RETURNS bool
AS $$SELECT plvdate.use_great_friday(true); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.use_great_friday() IS 'Great Friday will be holiday';

CREATE FUNCTION plvdate.unuse_great_friday()
RETURNS bool
AS $$SELECT plvdate.use_great_friday(false); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.unuse_great_friday() IS 'Great Friday will not be holiday';

CREATE FUNCTION plvdate.using_great_friday()
RETURNS bool
AS 'MODULE_PATHNAME','plvdate_using_great_friday'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.using_great_friday() IS 'Use Great Friday?';

CREATE OR REPLACE FUNCTION oracle.round(double precision, int)
RETURNS numeric
AS $$SELECT pg_catalog.round($1::numeric, $2)$$
LANGUAGE sql;

CREATE OR REPLACE FUNCTION oracle.trunc(double precision, int)
RETURNS numeric
AS $$SELECT pg_catalog.trunc($1::numeric, $2)$$
LANGUAGE sql;

CREATE OR REPLACE FUNCTION oracle.round(float, int)
RETURNS numeric
AS $$SELECT pg_catalog.round($1::numeric, $2)$$
LANGUAGE sql;

CREATE OR REPLACE FUNCTION oracle.trunc(float, int)
RETURNS numeric
AS $$SELECT pg_catalog.trunc($1::numeric, $2)$$
LANGUAGE sql;
