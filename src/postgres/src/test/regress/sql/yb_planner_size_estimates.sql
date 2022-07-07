CREATE TABLE s_10000 (id int, r int, val1 int, val2 int, val3 int, val4 int, PRIMARY KEY (id, r));
CREATE TABLE s_1000 (id int PRIMARY KEY, val int);
CREATE TABLE s_100 (id int PRIMARY KEY, val int);

INSERT INTO s_10000 SELECT i, i, i, i % 1000, i % 100, i % 10 FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO s_1000 SELECT i, i FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO s_100 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;

SET yb_enable_optimizer_statistics = FALSE;

ANALYZE;
\i sql/yb_planner_size_estimates_cmds.sql

-- When we have table statistics enabled, we use Postgres's selectivity functions to estimate how
-- many rows a relation will return, taking into account the supplied constraints.
SET yb_enable_optimizer_statistics = TRUE;

\i sql/yb_planner_size_estimates_cmds.sql

EXPLAIN SELECT * FROM s_10000 WHERE val1 < -100000;
EXPLAIN SELECT * FROM s_10000 WHERE id < -100000;
SET enable_indexscan = FALSE;
EXPLAIN SELECT * FROM s_10000 WHERE val1 < -100000;
EXPLAIN SELECT * FROM s_10000 WHERE id < -100000;