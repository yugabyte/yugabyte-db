CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
SELECT 1/0;   -- divide by zero
SELECT * FROM unknown; -- unknown table
ELECET * FROM unknown; -- syntax error

do $$
BEGIN
RAISE WARNING 'warning message';
END $$;

SELECT query, elevel, sqlcode, message FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
