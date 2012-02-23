/* contrib/pg_last_xact_activity/pg_last_xact_activity--1.0.sql */

-- Register functions.
CREATE FUNCTION pg_add_hint(text)
RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_clear_hint()
RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_dump_hint()
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_enable_hint(bool)
RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_enable_log(bool)
RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C;

