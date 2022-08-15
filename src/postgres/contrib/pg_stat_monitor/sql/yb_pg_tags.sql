CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
-- https://github.com/yugabyte/yugabyte-db/issues/11801
-- TODO: Tag parsing logic does not work on some YB builds
-- SELECT 1 AS num /* { "application", psql_app, "real_ip", 192.168.1.3) */;
-- SELECT query, comments FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT 1 AS num;
SELECT query FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
