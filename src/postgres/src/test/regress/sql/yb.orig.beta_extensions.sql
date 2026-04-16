-- Test that pg_stat_monitor shows beta warning when ysql_beta_features is false
SET client_min_messages = warning;

-- Test installing pg_stat_monitor gives beta warning
CREATE EXTENSION pg_stat_monitor;
-- Clean up
DROP EXTENSION pg_stat_monitor;
