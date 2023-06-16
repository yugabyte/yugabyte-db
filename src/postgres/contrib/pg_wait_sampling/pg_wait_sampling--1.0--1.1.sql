/* contrib/pg_wait_sampling/pg_wait_sampling--1.0--1.1.sql */

DROP FUNCTION pg_wait_sampling_get_current (
	pid int4,
	OUT pid int4,
	OUT event_type text,
	OUT event text
) CASCADE;

DROP FUNCTION pg_wait_sampling_get_history (
	OUT pid int4,
	OUT ts timestamptz,
	OUT event_type text,
	OUT event text
) CASCADE;

DROP FUNCTION pg_wait_sampling_get_profile (
	OUT pid int4,
	OUT event_type text,
	OUT event text,
	OUT count bigint
) CASCADE;

CREATE FUNCTION pg_wait_sampling_get_current (
	pid int4,
	OUT pid int4,
	OUT event_type text,
	OUT event text,
	OUT queryid int8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE CALLED ON NULL INPUT;

CREATE VIEW pg_wait_sampling_current AS
	SELECT * FROM pg_wait_sampling_get_current(NULL::integer);

GRANT SELECT ON pg_wait_sampling_current TO PUBLIC;

CREATE FUNCTION pg_wait_sampling_get_history (
	OUT pid int4,
	OUT ts timestamptz,
	OUT event_type text,
	OUT event text,
	OUT queryid int8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

CREATE VIEW pg_wait_sampling_history AS
	SELECT * FROM pg_wait_sampling_get_history();

GRANT SELECT ON pg_wait_sampling_history TO PUBLIC;

CREATE FUNCTION pg_wait_sampling_get_profile (
	OUT pid int4,
	OUT event_type text,
	OUT event text,
	OUT queryid int8,
	OUT count int8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

CREATE VIEW pg_wait_sampling_profile AS
	SELECT * FROM pg_wait_sampling_get_profile();

GRANT SELECT ON pg_wait_sampling_profile TO PUBLIC;
