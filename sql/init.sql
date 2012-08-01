SET search_path TO public;

CREATE TABLE t1 (id int PRIMARY KEY, val int);
CREATE TABLE t2 (id int PRIMARY KEY, val int);
CREATE TABLE t3 (id int PRIMARY KEY, val int);
CREATE TABLE t4 (id int PRIMARY KEY, val int);
CREATE TABLE p1 (id int PRIMARY KEY, val int);
CREATE TABLE p1_c1 (LIKE p1 INCLUDING ALL, CHECK (id <= 100)) INHERITS(p1);
CREATE TABLE p1_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 100 AND id <= 200)) INHERITS(p1);
CREATE TABLE p1_c3 (LIKE p1 INCLUDING ALL, CHECK (id > 200 AND id <= 300)) INHERITS(p1);
CREATE TABLE p1_c4 (LIKE p1 INCLUDING ALL, CHECK (id > 300)) INHERITS(p1);
CREATE TABLE p1_c1_c1 (LIKE p1 INCLUDING ALL, CHECK (id <= 50)) INHERITS(p1_c1);
CREATE TABLE p1_c1_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 50 AND id <= 100)) INHERITS(p1_c1);
CREATE TABLE p1_c3_c1 (LIKE p1 INCLUDING ALL, CHECK (id > 200 AND id <= 250)) INHERITS(p1_c3);
CREATE TABLE p1_c3_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 250 AND id <= 300)) INHERITS(p1_c3);

INSERT INTO t1 SELECT i, i % 100 FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t2 SELECT i, i % 10 FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t3 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO t4 SELECT i, i FROM (SELECT generate_series(1, 10) i) t;
INSERT INTO p1_c1_c1 SELECT i, i % 100 FROM (SELECT generate_series(1, 50) i) t;
INSERT INTO p1_c1_c2 SELECT i, i % 100 FROM (SELECT generate_series(51, 100) i) t;
INSERT INTO p1_c2 SELECT i, i % 100 FROM (SELECT generate_series(101, 200) i) t;
INSERT INTO p1_c3_c1 SELECT i, i % 100 FROM (SELECT generate_series(201, 250) i) t;
INSERT INTO p1_c3_c2 SELECT i, i % 100 FROM (SELECT generate_series(251, 300) i) t;
INSERT INTO p1_c4 SELECT i, i % 100 FROM (SELECT generate_series(301, 400) i) t;

CREATE INDEX t1_val ON t1 (val);
CREATE INDEX t2_val ON t2 (val);

ANALYZE t1;
ANALYZE t2;
ANALYZE t3;
ANALYZE t4;
ANALYZE p1;
ANALYZE p1_c1;
ANALYZE p1_c2;

CREATE VIEW v1 AS SELECT id, val FROM t1;
CREATE VIEW v2 AS SELECT t1.id t1_id, t1.val t1_val, t2.id t2_id, t2.val t2_val FROM t1, t2 WHERE t1.id = t2.id;
CREATE VIEW v3 AS SELECT t_1.id t1_id, t_1.val t1_val, t_2.id t2_id, t_2.val t2_val FROM t1 t_1, t2 t_2 WHERE t_1.id = t_2.id;
CREATE VIEW v4 AS SELECT v_2.t1_id, t_3.id FROM v2 v_2, t3 t_3 WHERE v_2.t1_id = t_3.id;

/*
 * The following GUC parameters need the setting of the default value to
 * succeed in regression test.
 */
SELECT name, setting, category
  FROM pg_settings
 WHERE category LIKE 'Query Tuning%'
    OR name = 'client_min_messages'
 ORDER BY category, name;
