CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
select pg_sleep(.5);
SELECT * FROM pg_stat_monitor_settings ORDER BY name COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
