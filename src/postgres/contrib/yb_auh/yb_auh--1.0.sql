-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yb_auh" to load this file. \quit

CREATE FUNCTION pg_active_universe_history(
    OUT auh_sample_time timestamptz,
    OUT top_level_request_id text,
    OUT request_id INTEGER,
    OUT wait_event_class text,
    OUT wait_event text
)

RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_active_universe_history'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

-- Register a view on the function for ease of use.
CREATE VIEW pg_active_universe_history AS
  SELECT * FROM pg_active_universe_history();

GRANT SELECT ON pg_active_universe_history TO PUBLIC;
