SET duckdb.force_execution = TRUE;

CREATE SCHEMA selinto_schema;

CREATE TABLE selinto_schema.tbl_nodata4 (a) AS

EXECUTE data_sel WITH NO DATA;

DROP SCHEMA selinto_schema CASCADE;
