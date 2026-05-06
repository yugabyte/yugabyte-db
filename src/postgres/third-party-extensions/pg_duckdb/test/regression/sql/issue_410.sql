-- In the past (see #410) we could cache the plan for the EXPLAIN query which
-- returns text columns. Instead of caching the plan of the actual query. This
-- is a regression test to make sure that this stays fixed.
SET duckdb.force_execution = ON;
CREATE TABLE t (a INT);
PREPARE f (INT) AS SELECT count(*) FROM t WHERE a = $1;
SET plan_cache_mode TO force_generic_plan;
EXPLAIN (COSTS OFF) EXECUTE f(2);
EXECUTE f(1); -- crash
-- cleanup
DROP TABLE t;
