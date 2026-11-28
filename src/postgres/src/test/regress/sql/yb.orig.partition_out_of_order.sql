-- Create a table with columns of different types and sizes.
CREATE TABLE base (a INT4 PRIMARY KEY, b TEXT, c BOOL) PARTITION BY RANGE (a);
-- Attach partitions with columns in different order.
CREATE TABLE part1 (c BOOL, b TEXT, a INT4 NOT NULL);
CREATE TABLE part2 (b TEXT, a INT4 PRIMARY KEY, c BOOL);
CREATE TABLE part3 (a INT4 UNIQUE NOT NULL, c BOOL, b TEXT);
ALTER TABLE base ATTACH PARTITION part1 FOR VALUES FROM (11) TO (21);
ALTER TABLE base ATTACH PARTITION part2 FOR VALUES FROM (21) TO (31);
ALTER TABLE base ATTACH PARTITION part3 FOR VALUES FROM (31) TO (41);

CREATE TABLE part4 (a BOOL, c TEXT, b INT4 PRIMARY KEY);
-- Error: names and types of columns should match
ALTER TABLE base ATTACH PARTITION part4 FOR VALUES FROM (41) TO (51);
DROP TABLE part4;

CREATE TABLE part5 (c INT4 PRIMARY KEY, a TEXT, B BOOL);
-- Error: names and types of columns should match
ALTER TABLE base ATTACH PARTITION part5 FOR VALUES FROM (51) TO (61);
DROP TABLE part5;

-- Attach a hierarchy of partitions with columns in different order
CREATE TABLE part6 (b TEXT, a INT NOT NULL, c BOOL) PARTITION BY LIST (a);
ALTER TABLE base ATTACH PARTITION part6 FOR VALUES FROM (61) TO (71);
CREATE TABLE part6a (a INT NOT NULL, b TEXT, c BOOL);
CREATE TABLE part6b (c BOOL, b TEXT, a INT NOT NULL);
ALTER TABLE part6 ATTACH PARTITION part6a FOR VALUES IN ((61), (62), (63), (64), (65));
ALTER TABLE part6 ATTACH PARTITION part6b FOR VALUES IN
 ((66), (67), (68), (69), (70));

CREATE TABLE partmisc (a INT NOT NULL, b TEXT, c BOOL);
ALTER TABLE base ATTACH PARTITION partmisc FOR VALUES FROM (100) TO (1000);

-- Inserts should be routed to the correct partition and inserted in the format
-- of the child partition.
INSERT INTO base VALUES (11, 'foo', TRUE);
INSERT INTO base VALUES (12, 'bar', TRUE), (21, 'baz', FALSE), (31, 'qux', TRUE);
INSERT INTO base (b, c, a) VALUES ('abc', 0::bool, 13), ('caz', TRUE, 22), ('xuq', FALSE, 32);
INSERT INTO base (b, a, c) VALUES ('part6a-abc', 61, FALSE), ('part6b-abc', 66, TRUE), ('partmisc-abc', 100, TRUE);
-- Error: When the main table is specified, the columns in the input should match the main table
-- rather than the partition to which the insert is routed.
INSERT INTO base VALUES (FALSE, 'def', 14);
-- Inserts that fetch values from a SELECT must also be of the correct format.
INSERT INTO base (SELECT b, c, a + 12 FROM part1);

BEGIN;
INSERT INTO base (a, b, c) (SELECT a + 12, b, c FROM part1);
INSERT INTO base (SELECT a + 12, b, c FROM part2);
SELECT * FROM base ORDER BY a;
ROLLBACK;

SELECT * FROM base ORDER BY a;

-- INSERTs that directly insert into a child partition.
INSERT INTO part1 (a, b) VALUES (14, 'not-def');
INSERT INTO part2 VALUES ('daz', 26, TRUE), ('faz', 27, FALSE);
INSERT INTO part3 (SELECT a + 12, c, b FROM part2 WHERE a IN (26, 27));

SELECT * FROM base ORDER BY a;

-- INSERT .. ON CONFLICT DO NOTHING cases
INSERT INTO base VALUES (11, 'doodoo', FALSE) ON CONFLICT DO NOTHING;
INSERT INTO base (a, b, c) VALUES (21, 'caz', TRUE) ON CONFLICT (a) DO NOTHING;
INSERT INTO base (c, b, a) VALUES (TRUE, 'abc', 26), (TRUE, 'abc', 26) ON CONFLICT (a) DO NOTHING;
INSERT INTO base (a, b, c) VALUES (11, 'aaa', TRUE), (31, 'ccc', FALSE), (21, 'bbb', TRUE) ON CONFLICT (a) DO NOTHING;
INSERT INTO base (a, b, c) VALUES (61, 'part6a-def', FALSE), (67, 'part6b-def', TRUE) ON CONFLICT DO NOTHING;
INSERT INTO part1 (a, b) VALUES (14, 'not-abc') ON CONFLICT DO NOTHING;
INSERT INTO part2 VALUES ('not-daz', 26, FALSE) ON CONFLICT (a) DO NOTHING;
INSERT INTO part3 (SELECT a + 6, c, b FROM part2 WHERE a IN (26, 27)) ON CONFLICT DO NOTHING;

SELECT * FROM base ORDER BY a;

-- INSERT .. ON CONFLICT DO UPDATE cases
-- Update partitioned column
INSERT INTO base VALUES (11, 'doodoo', FALSE), (12, 'zoozoo', FALSE), (21, 'bazbaz', FALSE) ON CONFLICT (a) DO UPDATE SET a = EXCLUDED.a;
INSERT INTO base VALUES (11, 'doodoo', FALSE), (12, 'zoozoo', FALSE), (21, 'bazbaz', FALSE) ON CONFLICT (a) DO UPDATE SET a = base.a;
INSERT INTO base (a, b) VALUES (15, 'ghi'), (62, 'part6a-def'), (100, 'partmisc-abc') ON CONFLICT (a) DO UPDATE SET a = EXCLUDED.a + 1;
-- Update both partitioned and non-partitioned columns
INSERT INTO base AS old (a, b, c) VALUES (11, 'doodoo', FALSE), (12, 'zoozoo', FALSE), (21, 'bazbaz', FALSE) ON CONFLICT (a) DO UPDATE SET b = old.b || 'z';
INSERT INTO base AS old (b, c, a) VALUES ('bar', TRUE, 13), ('baz', FALSE, 22), ('qux', TRUE, 33) ON CONFLICT (a) DO UPDATE SET b = old.b || 'z';
INSERT INTO base AS old (a) VALUES (22), (32), (62) ON CONFLICT (a) DO UPDATE SET b = old.b || 'y', a = EXCLUDED.a;
-- Update multiple columns in the column list format
INSERT INTO base AS old (a) VALUES (22), (32), (62) ON CONFLICT (a) DO UPDATE SET (b, a, c) = (old.b || 'y', EXCLUDED.a, NOT old.c);
-- Update columns with implicit type casting
INSERT INTO base (a, b, c) VALUES (38, 'dazzle', TRUE) ON CONFLICT (a) DO UPDATE SET (c, b, a) = (0::BOOL, 'dazz'::VARCHAR, 38.0);
-- Perform constraint checking as a constraint
INSERT INTO base AS old (a) VALUES (11), (102), (68) ON CONFLICT ON CONSTRAINT base_pkey DO UPDATE SET b = old.b || 'z';
-- Error: Moving between partitions should be recognized and disallowed.
INSERT INTO base VALUES(11, 'aaa', TRUE), (21, 'bbb', TRUE) ON CONFLICT (a) DO UPDATE SET a = base.a + 15;
INSERT INTO base VALUES(61, 'aaa', TRUE), (66, 'bbb', TRUE) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b || 'z', a = base.a + 5;

INSERT INTO part1 VALUES (TRUE, 'def', 14), (TRUE, 'ghi', 15) ON CONFLICT (a) DO UPDATE SET b = 'not-' || part1.b;
INSERT INTO part3 AS old (a, b, c) VALUES (31, 'qux', TRUE), (39, 'fax', TRUE) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b || 'x';
INSERT INTO part6 VALUES ('part6a-def', 61, FALSE), ('part6b-def', 66, TRUE) ON CONFLICT (a) DO UPDATE SET (b, c) = (EXCLUDED.b || 'z', NOT EXCLUDED.c);

SELECT * FROM base ORDER BY a;

-- UPDATE cases
-- Updates affecting a single partition to the correct partition and in the correct format.
UPDATE base SET a = a, b = 'y' || b WHERE a IN (21, 31);
-- Updates spanning multiples partitions must supply tuples in the correct format.
UPDATE base SET b = 'updated-' || b WHERE a % 10 = 2;
UPDATE base SET (c, b) = ((c::INT + 1)::BOOL, 'test-' || b) WHERE a % 10 = 6;
-- Updates affecting the partition key should be handled correctly when the
-- affected row moves between partitions.
UPDATE base SET a = a + 10, b = b || 'z' WHERE a IN (15, 27, 101);
-- Updates targeting a specific partition should work as expected.
UPDATE part1 SET c = NOT c, a = a + 2 WHERE a BETWEEN 60 AND 65 RETURNING *; -- no rows affected
UPDATE part6a SET c = NOT c, a = a + 2 WHERE a BETWEEN 60 AND 65 RETURNING c, b, a;
UPDATE part2 SET b = 'y' || b WHERE a % 10 = 1;
UPDATE part3 SET (b, c) = (b || 'z', NOT c) WHERE a >= 33 AND a <= 38;

SELECT * FROM base ORDER BY a;
TRUNCATE base;

-- GH-25147: Test UPDATE projections with partitions out-of-order.
INSERT INTO base VALUES (11, 'foo', TRUE), (21, 'bar', TRUE), (31, 'baz', FALSE);
UPDATE base SET (a, b) = (SELECT base.a, base.b || 'z');
UPDATE base SET (b, c, a) = (SELECT base.b || 'y', NOT base.c, base.a);
UPDATE base SET (b, c, a) = (SELECT base.b || 'y', base.c, base.a + 1);
INSERT INTO base (a, b) VALUES (12, 'foo'), (32, 'baz') ON CONFLICT(a) DO UPDATE SET (a, b) = (SELECT EXCLUDED.a, base.b || 'x');

SELECT * FROM base ORDER BY a;

-- GH-36896: ON CONFLICT DO UPDATE on a routed partition must fire the
-- UPDATE OF <col> triggers of the updated column, using the partition's
-- attribute numbers. A plain UPDATE is the control.
CREATE FUNCTION gh36896_say() RETURNS trigger LANGUAGE plpgsql AS
  $$BEGIN RAISE NOTICE 'fired %', TG_NAME; RETURN NEW; END$$;
-- Partition with reordered columns.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE TRIGGER upd_c AFTER UPDATE OF c ON t FOR EACH ROW EXECUTE FUNCTION gh36896_say();
CREATE TRIGGER upd_a BEFORE UPDATE OF a ON t FOR EACH ROW EXECUTE FUNCTION gh36896_say();
INSERT INTO t VALUES (1, 1, 1);
INSERT INTO t VALUES (1, 5, 5) ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c;
UPDATE t SET c = 9 WHERE a = 1;
SELECT * FROM t;
DROP TABLE t;
-- Partition with a dropped column, so attribute numbers shift without a
-- visible reorder.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (x int, a int NOT NULL, b int, c int, PRIMARY KEY (a));
ALTER TABLE t_p DROP COLUMN x;
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE TRIGGER upd_b AFTER UPDATE OF b ON t FOR EACH ROW EXECUTE FUNCTION gh36896_say();
INSERT INTO t VALUES (1, 1, 1);
INSERT INTO t VALUES (1, 5, 5) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
UPDATE t SET b = 9 WHERE a = 1;
SELECT * FROM t;
DROP TABLE t;
-- Two levels: only the sub-partition is reordered.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_mid PARTITION OF t FOR VALUES FROM (0) TO (100) PARTITION BY RANGE (a);
CREATE TABLE t_leaf (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t_mid ATTACH PARTITION t_leaf FOR VALUES FROM (0) TO (50);
CREATE TRIGGER upd_c AFTER UPDATE OF c ON t FOR EACH ROW EXECUTE FUNCTION gh36896_say();
INSERT INTO t VALUES (1, 1, 1);
INSERT INTO t VALUES (1, 5, 5) ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c;
UPDATE t SET c = 9 WHERE a = 1;
SELECT * FROM t;
DROP TABLE t;
-- Trigger defined on the reordered partition itself. ON CONFLICT through the
-- parent routes the row; ON CONFLICT on the partition does not.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE TRIGGER p_upd_c AFTER UPDATE OF c ON t_p FOR EACH ROW EXECUTE FUNCTION gh36896_say();
INSERT INTO t VALUES (1, 1, 1);
INSERT INTO t VALUES (1, 5, 5) ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c;
INSERT INTO t_p (a, b, c) VALUES (1, 6, 6) ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c;
SELECT * FROM t;
DROP TABLE t;
DROP FUNCTION gh36896_say;

-- GH-36896: more cases where the routed partition's attribute numbers differ
-- from the root's.
CREATE FUNCTION gh36896_log() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    RAISE NOTICE '% (% %) on %: %', TG_NAME, TG_WHEN, TG_OP, TG_TABLE_NAME, OLD;
    RETURN OLD;
  END IF;
  RAISE NOTICE '% (% %) on %: %', TG_NAME, TG_WHEN, TG_OP, TG_TABLE_NAME, NEW;
  RETURN NEW;
END$$;
-- Multi-row ON CONFLICT DO UPDATE across a partition with the root's layout
-- and a reordered partition.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_1 PARTITION OF t FOR VALUES FROM (0) TO (10);
CREATE TABLE t_2 (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_2 FOR VALUES FROM (10) TO (20);
CREATE TRIGGER upd_c AFTER UPDATE OF c ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
CREATE TRIGGER upd_a AFTER UPDATE OF a ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
INSERT INTO t VALUES (1, 1, 1), (11, 11, 11);
INSERT INTO t VALUES (1, 2, 2), (11, 12, 12)
  ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c RETURNING *;
SELECT * FROM t ORDER BY a;
DROP TABLE t;
-- Cross-partition UPDATE (row movement) into and out of a reordered
-- partition fires DELETE and INSERT triggers with the right row layout.
CREATE TABLE t (a int, b text, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_1 PARTITION OF t FOR VALUES FROM (0) TO (10);
CREATE TABLE t_2 (c int, b text, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_2 FOR VALUES FROM (10) TO (20);
CREATE TRIGGER bef_ins BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
CREATE TRIGGER aft_ins AFTER INSERT ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
CREATE TRIGGER bef_del BEFORE DELETE ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
CREATE TRIGGER aft_upd_c AFTER UPDATE OF c ON t FOR EACH ROW EXECUTE FUNCTION gh36896_log();
INSERT INTO t VALUES (1, 'x', 1);
UPDATE t SET a = 11, c = 7 WHERE a = 1 RETURNING *;
UPDATE t SET c = 8 WHERE a = 11 RETURNING *;
UPDATE t SET a = 2 WHERE a = 11 RETURNING *;
SELECT * FROM t ORDER BY a;
DROP TABLE t;
-- ON CONFLICT DO UPDATE of a stored generated column's source column, where
-- the source column moved, with an index on the generated column.
CREATE TABLE t (a int, b int, g int GENERATED ALWAYS AS (b * 10) STORED,
  PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (b int, g int GENERATED ALWAYS AS (b * 10) STORED,
  a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE INDEX t_g ON t (g);
INSERT INTO t (a, b) VALUES (1, 1);
INSERT INTO t (a, b) VALUES (1, 5) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
SELECT a, b, g FROM t;
SELECT yb_index_check('t_p_g_idx'::regclass);
DROP TABLE t;
-- ON CONFLICT DO UPDATE with a secondary index on a moved column: the index
-- is updated when its column changes and left alone otherwise.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE INDEX t_c ON t (c);
INSERT INTO t VALUES (1, 1, 1);
INSERT INTO t VALUES (1, 5, 5) ON CONFLICT (a) DO UPDATE SET c = EXCLUDED.c;
INSERT INTO t VALUES (1, 6, 6) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
SELECT * FROM t;
SELECT yb_index_check('t_p_c_idx'::regclass);
DROP TABLE t;
-- Column privileges: a constraint violation on a routed, reordered partition
-- must only show columns the role may see (CVE-2021-3393).
CREATE TABLE t (a int, b int, secret int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_1 PARTITION OF t FOR VALUES FROM (100) TO (200);
CREATE TABLE t_p (secret int, b int CHECK (b < 100), a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
INSERT INTO t VALUES (1, 1, 42), (101, 1, 43);
CREATE ROLE regress_gh36896;
GRANT INSERT (a, b), UPDATE (a, b), SELECT (a, b) ON t TO regress_gh36896;
SET ROLE regress_gh36896;
INSERT INTO t (a, b) VALUES (1, 1) ON CONFLICT (a) DO UPDATE SET b = 500;
INSERT INTO t (a, b) VALUES (2, 500);
UPDATE t SET a = 3, b = 500 WHERE a = 101;
RESET ROLE;
SELECT * FROM t ORDER BY a;
DROP TABLE t;
-- COPY into a partitioned table with a reordered partition and a BEFORE
-- INSERT trigger that changes a column.
CREATE TABLE t (a int, b int, c int, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE t_p (c int, b int, a int NOT NULL, PRIMARY KEY (a));
ALTER TABLE t ATTACH PARTITION t_p FOR VALUES FROM (0) TO (100);
CREATE FUNCTION gh36896_set_c() RETURNS trigger LANGUAGE plpgsql AS
  $$BEGIN NEW.c := NEW.b * 100; RETURN NEW; END$$;
CREATE TRIGGER set_c BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION gh36896_set_c();
COPY t FROM stdin WITH (ROWS_PER_TRANSACTION 0);
1	2	3
\.
COPY t (b, a) FROM stdin WITH (ROWS_PER_TRANSACTION 0);
4	5
\.
SELECT * FROM t ORDER BY a;
DROP TABLE t;
DROP FUNCTION gh36896_log, gh36896_set_c;
DROP ROLE regress_gh36896;
