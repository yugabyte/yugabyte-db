SET search_path TO public;

CREATE EXTENSION pg_hint_plan;
CREATE SCHEMA s0;

CREATE TABLE t1 (id int PRIMARY KEY, val int);
CREATE TABLE t2 (id int PRIMARY KEY, val int);
CREATE TABLE t3 (id int PRIMARY KEY, val int);
CREATE TABLE t4 (id int PRIMARY KEY, val int);
CREATE TABLE t5 (id int PRIMARY KEY, val int);
CREATE TABLE p1 (id int PRIMARY KEY, val int);
-- TODOs INHERITANCE is unsupported
/*
CREATE TABLE p1_c1 (LIKE p1 INCLUDING ALL, CHECK (id <= 100)) INHERITS(p1);
CREATE TABLE p1_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 100 AND id <= 200)) INHERITS(p1);
CREATE TABLE p1_c3 (LIKE p1 INCLUDING ALL, CHECK (id > 200 AND id <= 300)) INHERITS(p1);
CREATE TABLE p1_c4 (LIKE p1 INCLUDING ALL, CHECK (id > 300)) INHERITS(p1);
CREATE TABLE p1_c1_c1 (LIKE p1 INCLUDING ALL, CHECK (id <= 50)) INHERITS(p1_c1);
CREATE TABLE p1_c1_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 50 AND id <= 100)) INHERITS(p1_c1);
CREATE TABLE p1_c3_c1 (LIKE p1 INCLUDING ALL, CHECK (id > 200 AND id <= 250)) INHERITS(p1_c3);
CREATE TABLE p1_c3_c2 (LIKE p1 INCLUDING ALL, CHECK (id > 250 AND id <= 300)) INHERITS(p1_c3);
*/
CREATE TABLE p2 (id int PRIMARY KEY, val text);
CREATE INDEX p2_id_val_idx ON p2 (id, val);
CREATE UNIQUE INDEX p2_val_idx ON p2 (val);
CREATE INDEX p2_ununi_id_val_idx ON p2 (val);
CREATE INDEX p2_val_idx_1 ON p2 USING hash (val);
CREATE INDEX p2_val_id_idx ON p2 (val, id);
CREATE INDEX p2_val_idx2 ON p2 (val COLLATE "C");
CREATE INDEX p2_val_idx3 ON p2 (val varchar_ops);
CREATE INDEX p2_val_idx4 ON p2 (val DESC NULLS LAST);
CREATE INDEX p2_val_idx5 ON p2 (val ASC NULLS FIRST);
CREATE INDEX p2_expr ON p2 ((val < '120'));
CREATE INDEX p2_expr2 ON p2 ((id * 2 < 120));
CREATE INDEX p2_val_idx6 ON p2 (val) WHERE val >= '50' AND val < '51';
CREATE INDEX p2_val_idx7 ON p2 (val) WHERE id < 120;
-- TODOs INHERITANCE is unsupported
/*
CREATE TABLE p2_c1 (LIKE p2 INCLUDING ALL, CHECK (id <= 100)) INHERITS(p2);
CREATE TABLE p2_c2 (LIKE p2 INCLUDING ALL, CHECK (id > 100 AND id <= 200)) INHERITS(p2);
CREATE TABLE p2_c3 (LIKE p2 INCLUDING ALL, CHECK (id > 200 AND id <= 300)) INHERITS(p2);
CREATE TABLE p2_c4 (LIKE p2 INCLUDING ALL, CHECK (id > 300)) INHERITS(p2);
CREATE TABLE p2_c1_c1 (LIKE p2 INCLUDING ALL, CHECK (id <= 50)) INHERITS(p2_c1);
CREATE TABLE p2_c1_c2 (LIKE p2 INCLUDING ALL, CHECK (id > 50 AND id <= 100)) INHERITS(p2_c1);
CREATE TABLE p2_c3_c1 (LIKE p2 INCLUDING ALL, CHECK (id > 200 AND id <= 250)) INHERITS(p2_c3);
CREATE TABLE p2_c3_c2 (LIKE p2 INCLUDING ALL, CHECK (id > 250 AND id <= 300)) INHERITS(p2_c3);
*/
CREATE TABLE s0.t1 (id int PRIMARY KEY, val int);

INSERT INTO t1 SELECT i, i % 100 FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t2 SELECT i, i % 10 FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t3 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO t4 SELECT i, i FROM (SELECT generate_series(1, 10) i) t;
INSERT INTO t5 SELECT i, i % 100 FROM (SELECT generate_series(1, 10000) i) t;
-- Queries that are dependent on tables that are created using including all and inherits.
/*
INSERT INTO p1_c1_c1 SELECT i, i % 100 FROM (SELECT generate_series(1, 50) i) t;
INSERT INTO p1_c1_c2 SELECT i, i % 100 FROM (SELECT generate_series(51, 100) i) t;
INSERT INTO p1_c2 SELECT i, i % 100 FROM (SELECT generate_series(101, 200) i) t;
INSERT INTO p1_c3_c1 SELECT i, i % 100 FROM (SELECT generate_series(201, 250) i) t;
INSERT INTO p1_c3_c2 SELECT i, i % 100 FROM (SELECT generate_series(251, 300) i) t;
INSERT INTO p1_c4 SELECT i, i % 100 FROM (SELECT generate_series(301, 400) i) t;
INSERT INTO p2_c1_c1 SELECT i, i % 100 FROM (SELECT generate_series(1, 50) i) t;
INSERT INTO p2_c1_c2 SELECT i, i % 100 FROM (SELECT generate_series(51, 100) i) t;
INSERT INTO p2_c2 SELECT i, i % 100 FROM (SELECT generate_series(101, 200) i) t;
INSERT INTO p2_c3_c1 SELECT i, i % 100 FROM (SELECT generate_series(201, 250) i) t;
INSERT INTO p2_c3_c2 SELECT i, i % 100 FROM (SELECT generate_series(251, 300) i) t;
INSERT INTO p2_c4 SELECT i, i % 100 FROM (SELECT generate_series(301, 400) i) t;
*/

CREATE INDEX t1_val ON t1 (val);
CREATE INDEX t2_val ON t2 (val);
CREATE INDEX t5_id1 ON t5 (id);
CREATE INDEX t5_id2 ON t5 (id);
CREATE INDEX t5_id3 ON t5 (id);
CREATE INDEX t5_val ON t5 (val);
-- Queries that are dependent on tables that are created using including all and inherits.
/*
DROP INDEX p2_c4_val_id_idx;
*/
CREATE INDEX p2_id2_val ON p2 (id, id, val);
/*
CREATE INDEX p2_c1_id2_val ON p2_c1 (id, id, val);
CREATE INDEX p2_c2_id2_val ON p2_c2 (id, id, val);
*/
CREATE INDEX p2_val2_id ON p2 (val, id, val);
CREATE INDEX t5_idaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ON t5 (id);
CREATE INDEX p1_val1 ON p1 (val);
CREATE INDEX p1_val2 ON p1 (val);
CREATE INDEX p1_val3 ON p1 (val);
-- Queries that are dependent on tables that are created using including all and inherits.
/*
CREATE INDEX p1_c1_val1 ON p1_c1 (val);
CREATE INDEX p1_c1_val2 ON p1_c1 (val);
CREATE INDEX p1_c1_val3 ON p1_c1 (val);
CREATE INDEX p1_c1_c1_val1 ON p1_c1_c1 (val);
CREATE INDEX p1_c1_c1_val2 ON p1_c1_c1 (val);
CREATE INDEX p1_c1_c1_val3 ON p1_c1_c1 (val);
CREATE INDEX p1_c1_c2_val1 ON p1_c1_c2 (val);
CREATE INDEX p1_c1_c2_val2 ON p1_c1_c2 (val);
CREATE INDEX p1_c1_c2_val3 ON p1_c1_c2 (val);
CREATE INDEX p1_c2_val1 ON p1_c2 (val);
CREATE INDEX p1_c2_val2 ON p1_c2 (val);
CREATE INDEX p1_c2_val3 ON p1_c2 (val);
CREATE INDEX p1_c3_val1 ON p1_c3 (val);
CREATE INDEX p1_c3_val2 ON p1_c3 (val);
CREATE INDEX p1_c3_val3 ON p1_c3 (val);
CREATE INDEX p1_c3_c1_val1 ON p1_c3_c1 (val);
CREATE INDEX p1_c3_c1_val2 ON p1_c3_c1 (val);
CREATE INDEX p1_c3_c1_val3 ON p1_c3_c1 (val);
CREATE INDEX p1_c3_c2_val1 ON p1_c3_c2 (val);
CREATE INDEX p1_c3_c2_val2 ON p1_c3_c2 (val);
CREATE INDEX p1_c3_c2_val3 ON p1_c3_c2 (val);
CREATE INDEX p1_c4_val1 ON p1_c4 (val);
CREATE INDEX p1_c4_val2 ON p1_c4 (val);
CREATE INDEX p1_c4_val3 ON p1_c4 (val);
*/

ANALYZE t1;
ANALYZE t2;
ANALYZE t3;
ANALYZE t4;
ANALYZE t5;
ANALYZE p1;
/*
ANALYZE p1_c1;
ANALYZE p1_c2;
*/
ANALYZE p2;

CREATE VIEW v1 AS SELECT id, val FROM t1;
CREATE VIEW v2 AS SELECT t1.id t1_id, t1.val t1_val, t2.id t2_id, t2.val t2_val FROM t1, t2 WHERE t1.id = t2.id;
CREATE VIEW v3 AS SELECT t_1.id t1_id, t_1.val t1_val, t_2.id t2_id, t_2.val t2_val FROM t1 t_1, t2 t_2 WHERE t_1.id = t_2.id;
CREATE VIEW v4 AS SELECT v_2.t1_id, t_3.id FROM v2 v_2, t3 t_3 WHERE v_2.t1_id = t_3.id;

/*
 * The following GUC parameters need the setting of the default value to
 * succeed in regression test.
 */

/* Fix auto-tunable parameters */
ALTER SYSTEM SET effective_cache_size TO 16384;
SELECT pg_reload_conf();
SET effective_cache_size TO 16384;

-- YB: modify view to avoid yb_ settings.
CREATE VIEW settings AS
SELECT name, setting, category
  FROM pg_settings
 WHERE category LIKE 'Query Tuning%'
   AND name NOT LIKE 'yb_%'
    OR name = 'client_min_messages'
 ORDER BY category, name;
SELECT * FROM settings;

ANALYZE;
