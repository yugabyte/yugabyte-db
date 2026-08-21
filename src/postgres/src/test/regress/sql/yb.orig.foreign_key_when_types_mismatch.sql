--
-- Testing foreign key constraint check when types mismatch.
--
-- This test ensures that the foreign key constraint check uses the
-- batched DocDB lookup path when types mismatch.
--
-- Templated type pairs run yb_commands/foreign_key_when_types_mismatch_helper.sql
-- after CREATE TABLE, CREATE UNIQUE INDEX parent_v1_key ... SPLIT INTO 1 TABLETS,
-- and setting v1-v5, trap_parent_val, trap_child_val.
-- The helper covers: CASCADE, multi row insert, COPY, out of range,
-- SET NULL, NO ACTION deferrable (including deferred COMMIT fail), RESTRICT deferrable,
-- partial null composite update, SET DEFAULT composite, and MATCH FULL.
--
-- Covered type pairs (each runs the helper via \i):
--  * int2 -> int4, int4 -> int8, int2 -> int8 (widening, implicit cast)
--  * int4 -> int2, int8 -> int2, int8 -> int4 (narrowing, assignment cast)
--  * int2 -> bigserial, int4 -> bigserial, int8 -> bigserial
--  * int2 -> serial, int4 -> serial, int2 -> smallserial, smallserial -> bigserial
--  * text -> varchar, varchar -> text
--
-- Inline-only (structural or catalog-specific; not in helper):
--  * VARCHAR(3)/TEXT length overflow (cannot use generic parent/child shape)
--  * composite FK (3+ columns), multi-parent FK, partitioned/attach partition
--  * pg_constraint/pg_cast/pg_proc cache, ON CONFLICT, self-referential FK
--

\set VERBOSITY default

SHOW yb_enable_fkey_batched_docdb_lookup_when_types_mismatch; -- should be true

-- int2 to int4
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 456
\set v2 458
\set v3 457
\set v4 460
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4 to int8
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 789
\set v2 791
\set v3 790
\set v4 792
\set v5 999
\set trap_parent_val -2147483648
\set trap_child_val 2147483648
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2 to int8
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 32000
\set v2 32002
\set v3 32001
\set v4 32004
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4 to bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 2023
\set v2 2025
\set v3 2024
\set v4 2026
\set v5 999
\set trap_parent_val -2147483648
\set trap_child_val 2147483648
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2 to smallserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLSERIAL) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 4
\set v2 6
\set v3 5
\set v4 7
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- smallserial to bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLSERIAL, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 49
\set v2 51
\set v3 50
\set v4 52
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4 to int2 (assignment cast): child INT references parent SMALLINT.
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 100
\set v2 200
\set v3 101
\set v4 10
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int8 to int2 (assignment cast): child BIGINT references parent SMALLINT.
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 20000
\set v2 20002
\set v3 20001
\set v4 10
\set v5 999
\set trap_parent_val -32767
\set trap_child_val 32769
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int8 to int4 (assignment cast): child BIGINT references parent INT.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 1000000
\set v2 1000002
\set v3 1000001
\set v4 10
\set v5 999
\set trap_parent_val -2147483648
\set trap_child_val 2147483648
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- varchar to text
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(12), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 world
\set v2 beta
\set v3 world_upd
\set v4 alpha
\set v5 missing
\set trap_parent_val xy_no_parent
\set trap_child_val xy_no_parent_text
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- text to varchar
CREATE TABLE parent(h INT PRIMARY KEY, v1 VARCHAR(12)) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 TEXT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set v1 hello
\set v2 beta
\set v3 hello_upd
\set v4 alpha
\set v5 missing
\set trap_parent_val xy_no_parent
\set trap_child_val xy_no_parent_text
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- Overflow case: parent VARCHAR(3), child TEXT
-- This test ensures that we error about non-existence, when inserting a longer text into
-- the child column than the parent column can hold, because it cannot exist in the parent table.
CREATE TABLE parent(h INT PRIMARY KEY, v1 VARCHAR(3) UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 TEXT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'abcdef'); -- should fail: cannot fit parent column
INSERT INTO parent VALUES (2, 'abc');
INSERT INTO child VALUES (100001, 'abcdef'); -- should fail: cannot exist in parent table
UPDATE parent SET v1 = 'abcdef' WHERE h = 2; -- should fail: cannot fit parent column
DROP TABLE child;
DROP TABLE parent;

-- Overflow case: parent TEXT, child VARCHAR(3)
-- Parent text update should fail: cannot fit child column.
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(3), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'abcdef');
INSERT INTO parent VALUES (2, 'abc');
INSERT INTO child VALUES (100001, 'abcdef'); -- should fail: cannot fit child column
INSERT INTO child VALUES (100002, 'abc');
UPDATE parent SET v1 = 'hello' WHERE h = 2; -- should fail: cannot fit child column
DROP TABLE child;
DROP TABLE parent;

-- Composite FK with mixed mismatch and same-type columns
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT, v2 BIGINT, v3 TEXT, UNIQUE(v1, v2, v3))
  SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, v2 BIGINT, v3 VARCHAR(12),
  FOREIGN KEY(v1, v2, v3) REFERENCES parent(v1, v2, v3)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 123, 9876543210, 'hello');
INSERT INTO child VALUES (1, 123, 9876543210, 'hello'); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 123, 9876543210, 'hello');
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_v3_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_v2_v3_fkey FOREIGN KEY(v1, v2, v3)
  REFERENCES parent(v1, v2, v3) ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = 124, v2 = 9876543211, v3 = 'hello2' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 124 AND v2 = 9876543211 AND v3 = 'hello2';
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Composite FK with 1 explicit and 3 implicit casts ==> 1 explicit cast
-- because this is a composite key, we ultimately use an explicit cast.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT, v2 SMALLINT, v3 TEXT, v4 VARCHAR(6),
  UNIQUE(v1, v2, v3, v4)) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT, v2 INT, v3 VARCHAR(6), v4 CHAR(6),
  FOREIGN KEY(v1, v2, v3, v4) REFERENCES parent(v1, v2, v3, v4)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100, 123, 'hello', 'world');
INSERT INTO child VALUES (1, 100, 123, 'hello', 'world'); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 100, 123, 'hello', 'world');
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_v3_v4_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_v2_v3_v4_fkey FOREIGN KEY(v1, v2, v3, v4)
  REFERENCES parent(v1, v2, v3, v4) ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = 101, v2 = 124, v3 = 'hello2', v4 = 'world2' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 101 AND v2 = 124 AND
  v3 = 'hello2' AND v4 = 'world2';
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Foreign key into multiple parents
CREATE TABLE parent1(h INT PRIMARY KEY, v1 INT8 UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE parent2(h INT PRIMARY KEY, v1 INT4 UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT2,
  FOREIGN KEY(v1) REFERENCES parent1(v1) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY(v1) REFERENCES parent2(v1) ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent1 VALUES (1, 1);
INSERT INTO parent2 VALUES (101, 1);
INSERT INTO child VALUES (100001, 1); -- warm up catalog cache
DELETE FROM child WHERE h = 100001; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 1);
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child DROP CONSTRAINT child_v1_fkey1;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey
  FOREIGN KEY(v1) REFERENCES parent1(v1) ON DELETE CASCADE ON UPDATE CASCADE;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey1
  FOREIGN KEY(v1) REFERENCES parent2(v1) ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent1 SET v1 = 2 WHERE h = 1; -- should fail: no matching parent key
SELECT count(*) FROM child WHERE v1 = 2;
DELETE FROM parent2 WHERE h = 101;
SELECT count(*) FROM child;
DELETE FROM parent1 WHERE h = 1;
SELECT count(*) FROM child;
DELETE FROM parent2 WHERE h = 101;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent1;
DROP TABLE parent2;

-- INSERT ... ON CONFLICT: varchar -> text FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(12), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma');
INSERT INTO child VALUES (100001, 'alpha'), (100002, 'beta');
INSERT INTO child VALUES (100001, 'alpha') ON CONFLICT (h) DO UPDATE SET v1 = 'gamma';
SELECT * from child ORDER BY h;
INSERT INTO child VALUES (100001, 'beta') ON CONFLICT (h) DO UPDATE SET v1 = 'delta';
SELECT * from child ORDER BY h;
DROP TABLE child;
DROP TABLE parent;

-- Self-referential FK: int4 -> int8 (ref_id references id).
CREATE TABLE parent(id INT8 PRIMARY KEY, ref_id INT4 REFERENCES parent(id)) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, NULL);
INSERT INTO parent VALUES (2, 1);
INSERT INTO parent VALUES (3, 2);
SELECT * FROM parent ORDER BY id;
INSERT INTO parent VALUES (4, 999); -- should fail: no matching parent key
INSERT INTO parent VALUES (5, 5);
DROP TABLE parent;

-- Tests for partitioned table
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 100), (2, 200), (3, 300);
INSERT INTO child VALUES (10, 100), (110, 200); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 100), (111, 200), (112, 100);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (120, 999); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 101;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned child + composite FK mismatch (int->bigint, varchar->text)
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 TEXT, UNIQUE(v1, v2)) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 VARCHAR(12)) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
ALTER TABLE child ADD CONSTRAINT child_v1_v2_fkey FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 100, 'alpha'), (2, 200, 'beta');
INSERT INTO child VALUES (10, 100, 'alpha'), (110, 200, 'beta'); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 100, 'alpha'), (111, 200, 'beta');
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (120, 100, 'alpha2'); -- should fail: no matching parent key
UPDATE parent SET v1 = 101, v2 = 'alpha2' WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 101 AND v2 = 'alpha2';
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned parent + non-partitioned child mismatch (int -> bigint)
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, UNIQUE(v1, h)) PARTITION BY RANGE (h);
CREATE TABLE parent_p1 PARTITION OF parent FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE parent_p2 PARTITION OF parent FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 TEXT, FOREIGN KEY(v1, h) REFERENCES parent(v1, h)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (10, 100), (11, 200), (110, 200), (120, 300), (130, 400);
INSERT INTO child VALUES (10, 100, 'alpha'), (110, 200, 'beta'); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 200, 'gamma'),
 (120, 300, 'delta'), (130, 400, 'epsilon');
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (190, 100, 'alpha2'); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 10;
SELECT count(*) FROM child WHERE v1 = 101 AND h = 10;
DELETE FROM parent WHERE h = 110;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned parent + partitioned child mismatch (int -> bigint)
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT, UNIQUE(v1, h)) PARTITION BY RANGE (h);
CREATE TABLE parent_p1 PARTITION OF parent FOR VALUES FROM (0) TO (10000) SPLIT INTO 1 TABLETS;
CREATE TABLE parent_p2 PARTITION OF parent FOR VALUES FROM (10000) TO (20000) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1, h) REFERENCES parent(v1, h)
  ON DELETE CASCADE ON UPDATE CASCADE) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (12000) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p3 PARTITION OF child FOR VALUES FROM (12000) TO (20000) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 10000), (2, 10000), (11001, 20000), (12002, 30000);
INSERT INTO child VALUES (1, 10000); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child
  VALUES (2, 10000), (11001, 20000), (12002, 30000);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (10, 10000); -- should fail: no matching parent key
UPDATE parent SET v1 = 10001 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 10001 AND h = 1;
DELETE FROM parent WHERE h = 11001;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned parent + attached partitions with dropped-column holes in key mapping.
-- v2 is added later to exercise add-column schema churn before attach.
CREATE TABLE parent(h INT, v1 BIGINT, UNIQUE(v1, h))
  PARTITION BY RANGE (h);
CREATE TABLE parent_p1(h INT, dropped_col1 TEXT, v1 BIGINT, dropped_col2 INT,
  dropped_col3 BOOL)
  SPLIT INTO 1 TABLETS;
CREATE TABLE parent_p2(h INT, v1 BIGINT, dropped_col4 TEXT, dropped_col5 INT)
  SPLIT INTO 1 TABLETS;
ALTER TABLE parent_p1 ADD COLUMN added_col1 TEXT;
ALTER TABLE parent_p1 ADD COLUMN added_col2 INT;
ALTER TABLE parent_p2 ADD COLUMN added_col3 BOOL;
ALTER TABLE parent_p2 ADD COLUMN added_col4 TEXT;
ALTER TABLE parent_p1 DROP COLUMN dropped_col1;
ALTER TABLE parent_p1 DROP COLUMN dropped_col2;
ALTER TABLE parent_p1 DROP COLUMN dropped_col3;
ALTER TABLE parent_p2 DROP COLUMN dropped_col4;
ALTER TABLE parent_p2 DROP COLUMN dropped_col5;
ALTER TABLE parent_p1 DROP COLUMN added_col1;
ALTER TABLE parent_p1 DROP COLUMN added_col2;
ALTER TABLE parent_p2 DROP COLUMN added_col3;
ALTER TABLE parent_p2 DROP COLUMN added_col4;
ALTER TABLE parent ADD COLUMN v2 INT;
ALTER TABLE parent_p1 ADD COLUMN v2 INT;
ALTER TABLE parent_p2 ADD COLUMN v2 INT;
ALTER TABLE parent_p1 ADD CONSTRAINT parent_p1_v1_v2_h_key UNIQUE(v1, v2, h);
ALTER TABLE parent_p2 ADD CONSTRAINT parent_p2_v1_v2_h_key UNIQUE(v1, v2, h);
ALTER TABLE parent ATTACH PARTITION parent_p1 FOR VALUES FROM (0) TO (100);
ALTER TABLE parent ATTACH PARTITION parent_p2 FOR VALUES FROM (100) TO (200);
ALTER TABLE parent ADD CONSTRAINT parent_v1_v2_h_key UNIQUE(v1, v2, h);
CREATE TABLE child(id INT PRIMARY KEY, h INT, v1 INT, v2 INT,
  FOREIGN KEY(v1, v2, h) REFERENCES parent(v1, v2, h)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent(h, v1, v2) VALUES (10, 100, 1), (20, 200, 2), (120, 300, 3), (130, 400, 4);
INSERT INTO child VALUES (1, 120, 300, 3); -- warm up catalog cache
DELETE FROM child WHERE id = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child
  VALUES (100001, 10, 100, 1), (100002, 20, 200, 2), (100003, 120, 300, 3),
  (100004, 10, 100, 1), (100005, 130, 400, 4);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (100010, 120, 300, 9); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 10;
SELECT count(*) FROM child WHERE id IN (100001, 100004) AND v1 = 101;
DELETE FROM parent WHERE h = 120;
SELECT count(*) FROM child WHERE h = 120;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- ATTACH PARTITION on partitioned child (varchar -> text FK): valid vs invalid rows.
CREATE TABLE parent(h INT PRIMARY KEY, k TEXT UNIQUE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'alpha'), (2, 'beta');
CREATE TABLE child(id INT PRIMARY KEY, rk VARCHAR(12) REFERENCES parent(k))
  PARTITION BY RANGE (id);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
INSERT INTO child VALUES (1, 'alpha');
-- Attach partition whose rows reference existing parent keys (validation should pass).
CREATE TABLE child_p_good(id INT PRIMARY KEY, rk VARCHAR(12)) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_good VALUES (101, 'beta'), (111, 'alpha');
ALTER TABLE child ATTACH PARTITION child_p_good FOR VALUES FROM (100) TO (200);
INSERT INTO child VALUES (102, 'beta');
SELECT * FROM child ORDER BY id;
INSERT INTO child VALUES (122, 'missing'); -- should fail: no matching parent key
-- Attach partition whose rows violate the FK (validation should fail).
CREATE TABLE child_p_bad(id INT PRIMARY KEY, rk VARCHAR(12)) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_bad VALUES (201, 'missing');
ALTER TABLE child ATTACH PARTITION child_p_bad FOR VALUES FROM (200) TO (300); -- should fail: FK validation fails
DROP TABLE child_p_bad;
DROP TABLE child_p_good;
DROP TABLE child_p1;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned child: int4 -> int2 FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 100), (2, 200), (3, 300);
INSERT INTO child VALUES (10, 100), (110, 200); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 100), (111, 200), (112, 100);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (120, 999); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 101;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned child: int8 -> int2 FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLINT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 100), (2, 200), (3, 300);
INSERT INTO child VALUES (10, 100), (110, 200); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 100), (111, 200), (112, 100);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (120, 999); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 101;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Partitioned child: int8 -> int4 FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT) PARTITION BY RANGE (h);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
CREATE TABLE child_p2 PARTITION OF child FOR VALUES FROM (100) TO (200) SPLIT INTO 1 TABLETS;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 100), (2, 200), (3, 300);
INSERT INTO child VALUES (10, 100), (110, 200); -- warm up catalog cache
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (11, 100), (111, 200), (112, 100);
COMMIT;
SELECT count(*) FROM child;
INSERT INTO child VALUES (120, 999); -- should fail: no matching parent key
UPDATE parent SET v1 = 101 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 101;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- ATTACH PARTITION: int4 -> int2 assignment cast.
CREATE TABLE parent(h INT PRIMARY KEY, k SMALLINT UNIQUE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100), (2, 200);
CREATE TABLE child(id INT PRIMARY KEY, rk INT REFERENCES parent(k))
  PARTITION BY RANGE (id);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
INSERT INTO child VALUES (1, 100);
CREATE TABLE child_p_good(id INT PRIMARY KEY, rk INT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_good VALUES (101, 200), (111, 100);
ALTER TABLE child ATTACH PARTITION child_p_good FOR VALUES FROM (100) TO (200);
INSERT INTO child VALUES (102, 200);
SELECT * FROM child ORDER BY id;
INSERT INTO child VALUES (122, 999); -- should fail: no matching parent key
CREATE TABLE child_p_bad(id INT PRIMARY KEY, rk INT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_bad VALUES (201, 999);
ALTER TABLE child ATTACH PARTITION child_p_bad FOR VALUES FROM (200) TO (300); -- should fail: FK validation fails
DROP TABLE child_p_bad;
DROP TABLE child_p_good;
DROP TABLE child_p1;
DROP TABLE child;
DROP TABLE parent;

-- ATTACH PARTITION: int8 -> int2 assignment cast.
CREATE TABLE parent(h INT PRIMARY KEY, k SMALLINT UNIQUE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100), (2, 200);
CREATE TABLE child(id INT PRIMARY KEY, rk BIGINT REFERENCES parent(k))
  PARTITION BY RANGE (id);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
INSERT INTO child VALUES (1, 100);
CREATE TABLE child_p_good(id INT PRIMARY KEY, rk BIGINT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_good VALUES (101, 200), (111, 100);
ALTER TABLE child ATTACH PARTITION child_p_good FOR VALUES FROM (100) TO (200);
INSERT INTO child VALUES (102, 200);
SELECT * FROM child ORDER BY id;
INSERT INTO child VALUES (122, 999); -- should fail: no matching parent key
CREATE TABLE child_p_bad(id INT PRIMARY KEY, rk BIGINT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_bad VALUES (201, 999);
ALTER TABLE child ATTACH PARTITION child_p_bad FOR VALUES FROM (200) TO (300); -- should fail: FK validation fails
DROP TABLE child_p_bad;
DROP TABLE child_p_good;
DROP TABLE child_p1;
DROP TABLE child;
DROP TABLE parent;

-- ATTACH PARTITION: int8 -> int4 assignment cast.
CREATE TABLE parent(h INT PRIMARY KEY, k INT UNIQUE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100), (2, 200);
CREATE TABLE child(id INT PRIMARY KEY, rk BIGINT REFERENCES parent(k))
  PARTITION BY RANGE (id);
CREATE TABLE child_p1 PARTITION OF child FOR VALUES FROM (0) TO (100) SPLIT INTO 1 TABLETS;
INSERT INTO child VALUES (1, 100);
CREATE TABLE child_p_good(id INT PRIMARY KEY, rk BIGINT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_good VALUES (101, 200), (111, 100);
ALTER TABLE child ATTACH PARTITION child_p_good FOR VALUES FROM (100) TO (200);
INSERT INTO child VALUES (102, 200);
SELECT * FROM child ORDER BY id;
INSERT INTO child VALUES (122, 999); -- should fail: no matching parent key
CREATE TABLE child_p_bad(id INT PRIMARY KEY, rk BIGINT) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_bad VALUES (201, 999);
ALTER TABLE child ATTACH PARTITION child_p_bad FOR VALUES FROM (200) TO (300); -- should fail: FK validation fails
DROP TABLE child_p_bad;
DROP TABLE child_p_good;
DROP TABLE child_p1;
DROP TABLE child;
DROP TABLE parent;

-- ALTER COLUMN on FK column: int4 -> int8 after narrowing child column to int4.
CREATE TABLE parent(k BIGINT PRIMARY KEY);
CREATE TABLE child(id INT PRIMARY KEY, v1 BIGINT NOT NULL REFERENCES parent(k));
INSERT INTO parent VALUES (1), (2), (3);
INSERT INTO child VALUES (10, 1), (20, 2);
ALTER TABLE child ALTER COLUMN v1 TYPE INT USING v1::int;
INSERT INTO child VALUES (30, 3000000000);
INSERT INTO child VALUES (40, 3);
SELECT * FROM child ORDER BY id;
INSERT INTO child VALUES (50, 99); -- should fail: no matching parent key
DROP TABLE child;
DROP TABLE parent;

-- test ensures that the constraint cache is updated correctly when the
-- pg_constraint table is updated after the foreign key constraint is created.
CREATE TABLE parent(k int4 PRIMARY KEY);
CREATE TABLE child(id int4 GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY, v1 int2 NOT NULL,
  CONSTRAINT child_v1_fkey FOREIGN KEY (v1) REFERENCES parent(k) DEFERRABLE INITIALLY DEFERRED);
INSERT INTO parent VALUES (1);
INSERT INTO child (v1) VALUES (1);
SELECT count(*) FROM child;
BEGIN;
  INSERT INTO child (v1) VALUES (2);
  INSERT INTO parent VALUES (2);
COMMIT;  -- should succeed
ALTER TABLE child ALTER CONSTRAINT child_v1_fkey DEFERRABLE INITIALLY IMMEDIATE;
BEGIN;
  INSERT INTO child (v1) VALUES (3);
  INSERT INTO parent VALUES (3);
COMMIT;  -- should FAIL
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Test when pg_cast changes, cache must be invalidated.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100);
INSERT INTO child VALUES (100001, 100); -- warm up catalog cache
DELETE FROM child WHERE h = 100001; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100002, 100);
COMMIT;
SELECT count(*) FROM child;
SET yb_non_ddl_txn_for_sys_tables_allowed = on;
SELECT castfunc AS initial_castfunc FROM pg_cast
  WHERE castsource = 'int2'::regtype AND casttarget = 'int4'::regtype
  \gset
UPDATE pg_cast SET castfunc = 128912 -- some random function id
  WHERE castsource = 'int2'::regtype AND casttarget = 'int4'::regtype;
UPDATE pg_cast SET castfunc = :'initial_castfunc' -- back to original function id
  WHERE castsource = 'int2'::regtype AND casttarget = 'int4'::regtype;
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; -- should perform a catalog cache flush
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100003, 100);
COMMIT;
SELECT count(*) FROM child;
SET yb_non_ddl_txn_for_sys_tables_allowed = off;
DROP TABLE child;
DROP TABLE parent;

-- Test when pg_proc changes, cache must be invalidated.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100);
INSERT INTO child VALUES (100001, 100); -- warm up catalog cache
DELETE FROM child WHERE h = 100001; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100002, 100);
COMMIT;
SELECT count(*) FROM child;
SET yb_non_ddl_txn_for_sys_tables_allowed = on;
SELECT castfunc AS int4_castfunc FROM pg_cast
  WHERE castsource = 'int2'::regtype AND casttarget = 'int4'::regtype
  \gset
SELECT proname AS initial_proname FROM pg_proc WHERE oid = (:int4_castfunc)
  \gset
UPDATE pg_proc SET proname = 'random-func' WHERE oid = (:int4_castfunc);
UPDATE pg_proc SET proname = (:'initial_proname') WHERE oid = (:int4_castfunc);
-- The following query should perform a catalog cache flush but because the function
-- oid 313 (function int4 for int2 to int4 cast) is a builtin function, it will not
-- perform a catalog lookup and hence, not perform a catalog cache flush.
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100003, 100);
COMMIT;
SELECT count(*) FROM child;
SET yb_non_ddl_txn_for_sys_tables_allowed = off;
DROP TABLE child;
DROP TABLE parent;

-- yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = false
SET yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = false; -- should fail

-- Connect to a new session and verify the GUC is set to false.
\c "options='-c yb_enable_fkey_batched_docdb_lookup_when_types_mismatch=false'"

SHOW yb_enable_fkey_batched_docdb_lookup_when_types_mismatch; -- should be false

-- int2 to int4
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT) SPLIT INTO 1 TABLETS;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 456);
INSERT INTO child VALUES (1, 456); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 456);
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = 457 WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 457;
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
INSERT INTO parent VALUES (1, 456);
INSERT INTO child VALUES (100002, 999); -- should fail: no matching parent key
SELECT count(*) FROM child;
TRUNCATE child, parent;
-- cleanup
DROP TABLE child;
DROP TABLE parent;

-- yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = true
SET yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = true; -- should fail
