CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
-- https://github.com/yugabyte/yugabyte-db/issues/11801
-- TODO: pg_stat_monitor_reset does not remove this from shared memory and hence this might appear
-- in subsequent pg_stat_monitor tables.
-- SELECT 1;
SELECT 1/0;   -- divide by zero

SELECT query, state_code, state FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
