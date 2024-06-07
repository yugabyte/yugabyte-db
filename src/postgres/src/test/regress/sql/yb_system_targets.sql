CREATE TABLE t_normal (k INT PRIMARY KEY, v INT);
CREATE TEMP TABLE t_temp(k INT PRIMARY KEY, v INT);
INSERT INTO t_normal VALUES (1,1);
INSERT INTO t_temp VALUES (1,1);

-- check system columns in YbSeqScan (#18485)
explain (costs off)
/*+ SeqScan(t_normal) */ SELECT tableoid::regclass, * FROM t_normal;
/*+ SeqScan(t_normal) */ SELECT tableoid::regclass, * FROM t_normal;
/*+ SeqScan(t_normal) */ SELECT xmin, * FROM t_normal;
/*+ SeqScan(t_normal) */ SELECT xmax, * FROM t_normal;
/*+ SeqScan(t_normal) */ SELECT ctid, * FROM t_normal;

-- check system columns in YbSeqScan on temp tables
SELECT tableoid::regclass, * FROM t_temp;
SELECT xmin, * FROM t_temp;
SELECT xmax, * FROM t_temp;
SELECT ctid, * FROM t_temp;
