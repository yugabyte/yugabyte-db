-- YB #32655: guard for a pg_duckdb backend FATAL (YBCCheckForInterrupts invoked off the main thread)
-- when DuckDB scans a YugabyteDB table on a worker thread.
--
-- "ORDER BY" makes DuckDB run the postgres_scan source as a task on a worker thread;
-- the YB table fetch then calls pggate's YBCCheckForInterrupts(), which LOG(FATAL)s off the
-- backend main thread -> backend crash. It is flaky and reproduces at the default
-- (multi-threaded) duckdb.threads. See #32655 for the full analysis.
--
-- Because it crashes the backend (which would abort the whole pg_regress run), we cannot exercise the
-- ordered export at the default thread setting here. `SET duckdb.threads = 1` removes DuckDB's worker
-- threads, so the scan runs on the backend main thread and the export is safe. This test pins that
-- mitigation and must stay green. Once #32655 is fixed the export should also succeed at the default
-- multi-threaded setting -- drop the `SET duckdb.threads = 1` line then to widen the guard.
CREATE TABLE yb_issue_32655_t (id int PRIMARY KEY, val text);
INSERT INTO yb_issue_32655_t VALUES (1, 'a'), (2, 'b');
-- Single-threaded DuckDB -> postgres_scan runs on the main thread -> no YBCCheckForInterrupts FATAL.
SET duckdb.threads = 1;
COPY (SELECT id, val FROM yb_issue_32655_t ORDER BY id) TO '/tmp/yb_issue_32655.parquet';
DROP TABLE yb_issue_32655_t;
