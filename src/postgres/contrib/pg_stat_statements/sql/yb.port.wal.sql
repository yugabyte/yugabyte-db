--
-- Validate WAL generation metrics
--
-- YB: reset up front for a clean baseline; pg_stat_statements is auto-created at initdb in YB.
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
-- YB note: YB has no PostgreSQL WAL (it persists via DocDB/RocksDB + Raft), so the pgss
-- wal_* counters stay 0 and the "> 0" checks below report f, unlike upstream.
SET pg_stat_statements.track_utility = FALSE;
-- YB: track_planning defaults to TRUE in YB (FALSE in upstream); set FALSE to match upstream.
SET pg_stat_statements.track_planning = FALSE;

CREATE TABLE pgss_wal_tab (a int, b char(20));

INSERT INTO pgss_wal_tab VALUES(generate_series(1, 10), 'aaa');
UPDATE pgss_wal_tab SET b = 'bbb' WHERE a > 7;
DELETE FROM pgss_wal_tab WHERE a > 9;
DROP TABLE pgss_wal_tab;

-- Check WAL is generated for the above statements
SELECT query, calls, rows,
wal_bytes > 0 as wal_bytes_generated,
wal_records > 0 as wal_records_generated,
wal_records >= rows as wal_records_ge_rows
FROM pg_stat_statements ORDER BY query COLLATE "C";
SELECT pg_stat_statements_reset() IS NOT NULL AS t;
