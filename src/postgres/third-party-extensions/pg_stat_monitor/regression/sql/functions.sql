DROP ROLE IF EXISTS su;
CREATE USER su WITH SUPERUSER;
SET ROLE su;

CREATE EXTENSION pg_stat_monitor;
CREATE USER u1;

SELECT pg_stat_monitor_reset();
SELECT routine_schema, routine_name, routine_type, data_type FROM information_schema.routines WHERE routine_schema = 'public' ORDER BY routine_name COLLATE "C";

SET ROLE u1;
SELECT routine_schema, routine_name, routine_type, data_type FROM information_schema.routines WHERE routine_schema = 'public' ORDER BY routine_name COLLATE "C";

SET ROLE su;
DROP USER u1;
DROP EXTENSION pg_stat_monitor;
DROP USER su;
