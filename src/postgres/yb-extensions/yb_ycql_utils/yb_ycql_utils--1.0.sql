/* contrib/yb_ycql_utils/yb_ycql_utils--1.0.sql */

CREATE FUNCTION ycql_stat_statements(
    OUT queryid             int8,
    OUT query               text,
    OUT is_prepared         bool,
    OUT calls               int8,
    OUT total_time          float8,
    OUT min_time            float8,
    OUT max_time            float8,
    OUT mean_time           float8,
    OUT stddev_time         float8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE VIEW ycql_stat_statements AS
    SELECT *
    FROM ycql_stat_statements();

GRANT SELECT ON ycql_stat_statements TO PUBLIC;
