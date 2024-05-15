CREATE EXTENSION IF NOT EXISTS pg_stat_monitor;
SELECT pg_stat_monitor_reset();
CREATE OR REPLACE FUNCTION add(int, int) RETURNS INTEGER AS
$$
BEGIN
	return (select $1 + $2);
END; $$ language plpgsql;

CREATE OR REPLACE function add2(int, int) RETURNS int as
$$
BEGIN
	return add($1,$2);
END;
$$ language plpgsql;

SELECT add2(1,2);
-- https://github.com/yugabyte/yugabyte-db/issues/11801
-- TODO: Top query has run to run variability in YB
-- SELECT query, top_query FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT query FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
