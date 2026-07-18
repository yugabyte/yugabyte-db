CREATE OR REPLACE FUNCTION generate_histogram()
    RETURNS TABLE (
    range TEXT, freq INT, bar TEXT
  )  AS $$
Declare
    bucket_id integer;
    query_id text;
BEGIN
    select bucket into bucket_id from pg_stat_monitor order by calls desc limit 1;
    select queryid into query_id from pg_stat_monitor order by calls desc limit 1;
    --RAISE INFO 'bucket_id %', bucket_id;
    --RAISE INFO 'query_id %', query_id;
    return query
    SELECT * FROM histogram(bucket_id, query_id) AS a(range TEXT, freq INT, bar TEXT);
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION run_pg_sleep(INTEGER) RETURNS VOID AS $$
DECLARE
    loops ALIAS FOR $1;
BEGIN
    FOR i IN 1..loops LOOP
	--RAISE INFO 'Current timestamp: %', timeofday()::TIMESTAMP;
	RAISE INFO 'Sleep % seconds', i;
	PERFORM pg_sleep(i);
    END LOOP;
END;
$$ LANGUAGE 'plpgsql' STRICT;

CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
Set pg_stat_monitor.pgsm_track='all';
select run_pg_sleep(5);

SELECT substr(query, 0,50) as query, calls, resp_calls FROM pg_stat_monitor ORDER BY query COLLATE "C";

select * from generate_histogram();

DROP EXTENSION pg_stat_monitor;
