CREATE TABLE t1 (id int PRIMARY KEY, val int);
CREATE TABLE t2 (id int PRIMARY KEY, val int);
CREATE TABLE t3 (id int PRIMARY KEY, val int);
CREATE TABLE t4 (id int PRIMARY KEY, val int);

INSERT INTO t1 SELECT i, i % 100 FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t2 SELECT i, i % 10 FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t3 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO t4 SELECT i, i FROM (SELECT generate_series(1, 10) i) t;

CREATE INDEX t1_val ON t1 (val);
CREATE INDEX t2_val ON t2 (val);

ANALYZE t1;
ANALYZE t2;
ANALYZE t3;
ANALYZE t4;

CREATE VIEW v1 AS SELECT id, val FROM t1;
CREATE VIEW v2 AS SELECT t1.id t1_id, t1.val t1_val, t2.id t2_id, t2.val t2_val FROM t1, t2 WHERE t1.id = t2.id;
CREATE VIEW v3 AS SELECT t_1.id t1_id, t_1.val t1_val, t_2.id t2_id, t_2.val t2_val FROM t1 t_1, t2 t_2 WHERE t_1.id = t_2.id;
CREATE VIEW v4 AS SELECT v_2.t1_id, t_3.id FROM v2 v_2, t3 t_3 WHERE v_2.t1_id = t_3.id;

SET enable_bitmapscan TO on;
SET enable_hashagg TO on;
SET enable_tidscan TO on;
SET enable_sort TO on;
SET enable_indexscan TO on;
SET enable_seqscan TO on;
SET enable_material TO on;
SET enable_hashjoin TO on;
SET enable_mergejoin TO on;
SET enable_nestloop TO on;
