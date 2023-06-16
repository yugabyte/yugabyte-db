CREATE EXTENSION pg_stat_statements;
CREATE EXTENSION pgsentinel;
select pg_sleep(3);
select count(*) > 0 AS has_data from pg_active_session_history where queryid in (select queryid from pg_stat_statements);
DROP EXTENSION pgsentinel;
DROP EXTENSION pg_stat_statements;
