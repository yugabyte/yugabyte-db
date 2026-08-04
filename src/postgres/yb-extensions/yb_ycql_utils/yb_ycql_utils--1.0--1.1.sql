/* yb-extensions/yb_ycql_utils/yb_ycql_utils--1.0--1.1.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION yb_ycql_utils UPDATE TO '1.1'" to load this file. \quit

/* First we have to remove them from the extension */
ALTER EXTENSION yb_ycql_utils DROP VIEW ycql_stat_statements;
ALTER EXTENSION yb_ycql_utils DROP FUNCTION ycql_stat_statements();

/* Then we can drop them */
DROP VIEW IF EXISTS ycql_stat_statements;
DROP FUNCTION IF EXISTS ycql_stat_statements();

CREATE FUNCTION ycql_stat_statements(
    OUT queryid             int8,
    OUT query               text,
    OUT is_prepared         bool,
    OUT calls               int8,
    OUT total_time          float8,
    OUT min_time            float8,
    OUT max_time            float8,
    OUT mean_time           float8,
    OUT stddev_time         float8,
    OUT keyspace            text
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE VIEW ycql_stat_statements AS
    SELECT *
    FROM ycql_stat_statements();

GRANT SELECT ON ycql_stat_statements TO PUBLIC;
