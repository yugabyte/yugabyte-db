CREATE EXTENSION pg_stat_monitor;
Set pg_stat_monitor.pgsm_extract_comments = 'yes'; 
SELECT pg_stat_monitor_reset();
SELECT 1 AS num /* { "application", psql_app, "real_ip", 192.168.1.3) */;
SELECT query, comments FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
