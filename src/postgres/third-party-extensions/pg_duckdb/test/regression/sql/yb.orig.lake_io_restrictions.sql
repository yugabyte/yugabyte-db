-- YB lake_io mode restrictions (see pgduckdb_guc.hpp).
-- lake_io is the default duckdb.execution_mode, so this file runs with no
-- special setup.  Nearly every statement below is a negative test: each
-- restricted operation must be rejected with the exact error pinned in the
-- expected output. The exceptions are the SHOW (pins the default) and the
-- plain YB DDL/DML, which must keep working unaffected by the clamp.
SHOW duckdb.execution_mode;

-- must fail: duckdb.execution_mode is PGC_INTERNAL, so even a superuser cannot change it.
SET duckdb.execution_mode = 'full';

-- must fail: force_execution cannot bypass the lake_io restrictions.
SET duckdb.force_execution = true;

-- must fail: duckdb.query() is arbitrary DuckDB SQL passthrough; in lake_io it is not routed to
-- DuckDB, so it resolves to the duckdb_only stub and errors.
SELECT * FROM duckdb.query('SELECT 1');
-- must fail: duckdb.raw_query() runs arbitrary DuckDB SQL directly (no planner hook), so it is
-- gated separately in lake_io mode.
SELECT duckdb.raw_query('SELECT 1');

CREATE TABLE yb_lake_restrict_t (id int PRIMARY KEY);
INSERT INTO yb_lake_restrict_t VALUES (1);

-- must fail: with the policy error, not file-not-found: the parquet file deliberately does
-- not exist, proving the mix check fires before DuckDB opens the file.
SELECT * FROM yb_lake_restrict_t JOIN read_parquet('/tmp/does-not-exist.parquet') AS r ON true;

-- DuckDB table AM disabled: CREATE, CREATE TABLE AS, and ALTER ... SET ACCESS METHOD
CREATE TABLE duckdb_restrict_t (id int) USING duckdb;
CREATE TABLE duckdb_restrict_ctas USING duckdb AS SELECT 1 AS id;
CREATE TABLE yb_convert_t (id int);
ALTER TABLE yb_convert_t SET ACCESS METHOD duckdb;

-- must fail:MotherDuck disabled
CALL duckdb.enable_motherduck();

-- must fail: Extension management disabled (install/load rejected for any extension)
SELECT duckdb.install_extension('delta');
SELECT duckdb.load_extension('json');

-- must fail: in lake_io mode only read_parquet/read_csv/read_json route a query to DuckDB, so
-- delta_scan no longer does and the query errors instead of executing in DuckDB.
SELECT * FROM delta_scan('/tmp/does-not-exist');
-- must fail: iceberg_scan (the other lake format) likewise drops off the DuckDB-only list.
SELECT * FROM iceberg_scan('/tmp/does-not-exist');
-- must fail: aggregates route through Aggref->aggfnoid (a different hook path than delta_scan's
-- FuncExpr->funcid), so a DuckDB-only aggregate must be rejected too.
SELECT approx_count_distinct(1);

-- Cleanup
DROP TABLE IF EXISTS yb_lake_restrict_t;
DROP TABLE IF EXISTS duckdb_restrict_t;
DROP TABLE IF EXISTS yb_convert_t;
