-- YB #32655: guard for a pg_duckdb backend FATAL (YBCCheckForInterrupts invoked off the main thread)
-- when DuckDB scans a YugabyteDB table on a worker thread.
--
-- "ORDER BY" makes DuckDB run the postgres_scan source as a task on a worker thread; the YB table
-- fetch then calls pggate's YBCCheckForInterrupts(), which used to LOG(FATAL) off the backend main
-- thread -> backend crash. Runs at the default (multi-threaded) duckdb.threads so that path stays
-- covered. Do not add `SET duckdb.threads`: it is an init-time DuckDB setting, and the connection
-- manager hands out pooled backends that already initialized DuckDB, so deploying the SET FATALs.
--
-- DuckDB locks the export file while writing it, and /tmp is shared by concurrently running
-- clusters, so the path is qualified with :PORT to keep it unique per cluster.
\set parquet_path '/tmp/yb_issue_32655_' :PORT '.parquet'
CREATE TABLE yb_issue_32655_t (id int PRIMARY KEY, val text);
INSERT INTO yb_issue_32655_t VALUES (1, 'a'), (2, 'b');
COPY (SELECT id, val FROM yb_issue_32655_t ORDER BY id) TO :'parquet_path';
DROP TABLE yb_issue_32655_t;
