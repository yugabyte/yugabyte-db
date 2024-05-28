CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
SELECT 1/0;   -- divide by zero
SELECT * FROM unknown; -- unknown table
-- https://github.com/yugabyte/yugabyte-db/issues/11801
-- TODO: works with postgres and not with YB
-- ELECET * FROM unknown; -- syntax error

do $$
BEGIN
RAISE WARNING 'warning message';
END $$;

-- TODO: SQL code sometimes did not work in mac and alma platforms for YB
-- SELECT query, elevel, sqlcode, message FROM pg_stat_monitor ORDER BY query COLLATE "C",elevel;
SELECT query, elevel, message FROM pg_stat_monitor ORDER BY query COLLATE "C",elevel;
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
