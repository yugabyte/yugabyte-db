CREATE TABLE t1 (val1 int, val2 int);
CREATE TABLE t2 (val1 int, val2 int);
CREATE TABLE t3 (val1 int, val2 int);
CREATE TABLE t4 (val1 int, val2 int);

CREATE VIEW v1 AS SELECT val1, val2 FROM t1;
CREATE VIEW v2 AS SELECT t1.val1 t1_val1, t1.val2 t1_val2, t2.val1 t2_val1, t2.val2 t2_val2 FROM t1, t2 WHERE t1.val1 = t2.val1;
CREATE VIEW v3 AS SELECT t_1.val1 t1_val1, t_1.val2 t1_val2, t_2.val1 t2_val1, t_2.val2 t2_val2 FROM t1 t_1, t2 t_2 WHERE t_1.val1 = t_2.val1;
CREATE VIEW v4 AS SELECT v_2.t1_val1, t_3.val1 FROM v2 v_2, t3 t_3 WHERE v_2.t1_val1 = t_3.val1;

INSERT INTO t1 SELECT i, i FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t2 SELECT i, i FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t3 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO t4 SELECT i, i FROM (SELECT generate_series(1, 10) i) t;

CREATE INDEX t1_val1 ON t1 (val1);
CREATE INDEX t2_val1 ON t2 (val1);
CREATE INDEX t3_val1 ON t3 (val1);
CREATE INDEX t4_val1 ON t4 (val1);

ANALYZE t1;
ANALYZE t2;
ANALYZE t3;
ANALYZE t4;

\set t1_oid `psql contrib_regression -tA -c "SELECT oid FROM pg_class WHERE relname = 't1'"`
\set t2_oid `psql contrib_regression -tA -c "SELECT oid FROM pg_class WHERE relname = 't2'"`
\set t3_oid `psql contrib_regression -tA -c "SELECT oid FROM pg_class WHERE relname = 't3'"`
\set t4_oid `psql contrib_regression -tA -c "SELECT oid FROM pg_class WHERE relname = 't4'"`

--SET enable_bitmapscan TO off;
--SET enable_hashagg TO off;
--SET enable_tidscan TO off;
--SET enable_sort TO off;
--SET enable_indexscan TO off;
--SET enable_seqscan TO off;
--SET enable_material TO off;
--SET enable_hashjoin TO off;
--SET enable_mergejoin TO off;
--SET enable_nestloop TO off;

EXPLAIN SELECT * FROM t1, t2 WHERE t1.val1 = t2.val1;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val2 = t2.val2;

CREATE EXTENSION pg_hint_plan;

EXPLAIN SELECT * FROM t1, t2 WHERE t1.val1 = t2.val1;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val2 = t2.val2;

SELECT pg_add_hint('nest(' || :t1_oid || ',' || :t2_oid || ')');
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val1 = t2.val1;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val2 = t2.val2;

SELECT pg_add_hint('hash(' || :t1_oid || ',' || :t2_oid || ')');
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val1 = t2.val1;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val2 = t2.val2;

SELECT pg_add_hint('merge(' || :t1_oid || ',' || :t2_oid || ')');
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val1 = t2.val1;
EXPLAIN SELECT * FROM t1, t2 WHERE t1.val2 = t2.val2;

SELECT pg_clear_hint();
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1;
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val2 = t2.val2 AND t2.val2 = t3.val2;

SELECT pg_add_hint('nest(' || :t1_oid || ',' || :t2_oid ||  ',' || :t3_oid || ')');
SELECT pg_add_hint('nest(' || :t1_oid || ',' || :t3_oid || ')');
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1;
EXPLAIN SELECT * FROM t1, t2, t3 WHERE t1.val2 = t2.val2 AND t2.val2 = t3.val2;

SELECT pg_clear_hint();
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
SELECT pg_add_hint('no_merge(' || :t1_oid || ',' || :t2_oid || ')');
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
SET enable_mergejoin TO off;
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
SELECT pg_add_hint('no_hash(' || :t3_oid || ',' || :t4_oid || ')');
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
SELECT pg_add_hint('no_nest(' || :t2_oid || ',' || :t3_oid || ',' || :t4_oid || ')');
EXPLAIN SELECT * FROM t1, t2, t3, t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;

SELECT pg_clear_hint();
SET join_collapse_limit TO 10;
EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
SET join_collapse_limit TO 1;
EXPLAIN SELECT * FROM t1 CROSS JOIN t2 CROSS JOIN t3 CROSS JOIN t4 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
EXPLAIN SELECT * FROM t2 CROSS JOIN t3 CROSS JOIN t4 CROSS JOIN t1 WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
EXPLAIN SELECT * FROM t1 CROSS JOIN (t2 CROSS JOIN t3 CROSS JOIN t4) WHERE t1.val1 = t2.val1 AND t2.val1 = t3.val1 AND t3.val1 = t4.val1;
EXPLAIN SELECT * FROM t1 JOIN t2 ON (t1.val1 = t2.val1) JOIN t3 ON (t2.val1 = t3.val1) JOIN t4 ON (t3.val1 = t4.val1);

EXPLAIN SELECT * FROM v2;
EXPLAIN SELECT * FROM v3 v_3;
EXPLAIN SELECT * FROM v2 v_2, v3 v_3 WHERE v_2.t1_val1 = v_3.t1_val1;

--SELECT pg_enable_log(true);
EXPLAIN SELECT * FROM v4 v_4;
SET from_collapse_limit TO 1;
EXPLAIN SELECT * FROM v4 v_4;
--SELECT pg_enable_log(false);
