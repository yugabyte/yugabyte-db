--
-- Testing foreign key constraint check when types mismatch.
--
-- This test ensures that the foreign key constraint check uses the
-- batched DocDB lookup path when types mismatch.
--
-- Covered cases include:
--  * int2 -> int4, int4 -> int8, int2 -> int8
--  * int2 -> bigserial, int4 -> bigserial, int8 -> bigserial
--  * int2 -> serial, int4 -> serial
--  * int2 -> smallserial
--  * smallserial -> serial, serial -> bigserial, smallserial -> bigserial
--  * text -> varchar, varchar -> text
--
-- In each test, we create a parent table and a child table with a foreign key constraint.
-- We then insert a row into the parent table and a row into the child table. We then update
-- the parent table and check that the row from the child table is also updated. We then delete
-- the row from the parent table and check that the row from the child table is also deleted.
-- We then drop the parent and child tables.
--

\set VERBOSITY default

-- int2_to_int4
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 456
\set updated_v1 457
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4_to_int8
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 789
\set updated_v1 790
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2_to_int8
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 32000
\set updated_v1 32001
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4_to_bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 2023
\set updated_v1 2024
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2_to_bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 4893
\set updated_v1 4894
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int8_to_bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 BIGINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 7890
\set updated_v1 7891
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2_to_serial
CREATE TABLE parent(h INT PRIMARY KEY, v1 SERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 8
\set updated_v1 9
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4_to_serial
CREATE TABLE parent(h INT PRIMARY KEY, v1 SERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 2023
\set updated_v1 2024
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int2_to_smallserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLSERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 4
\set updated_v1 5
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- smallserial_to_bigserial
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGSERIAL UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLSERIAL, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 49
\set updated_v1 50
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- int4_to_int2 out-of-range should not match by truncation.
-- This test would fail if we implicitly typecast the INT4 value to INT2.
-- We do not implicitly typecast the INT4 value to INT2 right now.
CREATE TABLE parent(h INT PRIMARY KEY, v1 SMALLINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
-- 32769 does not fit SMALLINT. If FK validation incorrectly truncates to int2,
-- it can wrap to -32767 and falsely match this parent row.
INSERT INTO parent VALUES (1, -32767);
INSERT INTO child VALUES (100001, 32769);
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- int2_to_int4
-- out of range of int2 value update on parent should fail because it cannot fit child column.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 456);
INSERT INTO child VALUES (100001, 456);
SELECT count(*) FROM child;
UPDATE parent SET v1 = 32769 WHERE h = 1; -- should fail because it cannot fit child column.
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 456;
DROP TABLE child;
DROP TABLE parent;

-- int4 to int8
-- out of range of int4 value update on parent should fail because it cannot fit child column.
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 456);
INSERT INTO child VALUES (100001, 456);
SELECT count(*) FROM child;
UPDATE parent SET v1 = 2147483648 WHERE h = 1; -- should fail because it cannot fit child column.
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 456;
DROP TABLE child;
DROP TABLE parent;

-- text_to_varchar
CREATE TABLE parent(h INT PRIMARY KEY, v1 VARCHAR(12) UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 TEXT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 hello
\set updated_v1 hello_upd
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- varchar_to_text
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(12), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
\set initial_v1 world
\set updated_v1 world_upd
\i yb_commands/foreign_key_when_types_mismatch_helper.sql

-- Overflow case: parent VARCHAR(3), child TEXT
-- This test ensures that we error about non-existence, when inserting a longer text into
-- the child column than the parent column can hold, because it cannot exist in the parent table.
CREATE TABLE parent(h INT PRIMARY KEY, v1 VARCHAR(3) UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 TEXT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'abcdef'); -- should fail because it cannot fit parent column.
INSERT INTO parent VALUES (2, 'abc');
INSERT INTO child VALUES (100001, 'abcdef'); -- should fail because it cannot exist in parent table.
UPDATE parent SET v1 = 'abcdef' WHERE h = 2; -- should fail because it cannot fit parent column.
DROP TABLE child;
DROP TABLE parent;

-- Overflow case: parent TEXT, child VARCHAR(3)
-- out of range of text value update on parent should fail because it cannot fit child column.
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(3), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'abcdef');
INSERT INTO parent VALUES (2, 'abc');
INSERT INTO child VALUES (100001, 'abcdef'); -- should fail because it cannot fit in child column.
INSERT INTO child VALUES (100002, 'abc');
UPDATE parent SET v1 = 'hello' WHERE h = 2; -- should fail because it cannot fit child column.
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

-- Composite FK with 2 explicit and 2 implicit casts ==> 3 read requests total
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 INT, v3 TEXT, v4 serial,
  UNIQUE(v1, v2, v3, v4)) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 BIGINT, v3 VARCHAR(6), v4 BIGINT,
  FOREIGN KEY(v1, v2, v3, v4) REFERENCES parent(v1, v2, v3, v4)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 123, 98765432, 'hello', 13890);
INSERT INTO child VALUES (1, 123, 98765432, 'hello', 13890); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 123, 98765432, 'hello', 13890);
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_v3_v4_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_v2_v3_v4_fkey FOREIGN KEY(v1, v2, v3, v4)
  REFERENCES parent(v1, v2, v3, v4) ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = 124, v2 = 98765433, v3 = 'hello2', v4 = 13891 WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 124 AND v2 = 98765433 AND
  v3 = 'hello2' AND v4 = 13891;
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
UPDATE parent1 SET v1 = 2 WHERE h = 1; -- should fail because parent2 does not have this value.
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 2;
DELETE FROM parent2 WHERE h = 101; -- should fail because parent1 does not have this value.
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 2;
DELETE FROM parent1 WHERE h = 1;
SELECT count(*) FROM child;
DELETE FROM parent2 WHERE h = 101;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent1;
DROP TABLE parent2;

-- Foreign key with multiple inserts should use fast path.
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 1);
INSERT INTO child VALUES (1, 1); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 1), (100002, 1), (100003, 1);
COMMIT;
SELECT count(*) FROM child;
UPDATE parent SET v1 = 2 WHERE h = 1;
SELECT count(*) FROM child WHERE v1 = 2;
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
-- Foreign key with multiple inserts with different lookup key should use fast path.
INSERT INTO parent VALUES (1, 1);
INSERT INTO parent VALUES (2, 2);
INSERT INTO child VALUES (1, 1); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, 1), (100002, 2);
COMMIT;
SELECT count(*) FROM child;
UPDATE parent SET v1 = 3 WHERE h = 2;
SELECT count(*) FROM child WHERE h = 100002 AND v1 = 3;
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- update with NULL value ==> should not update child table
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 TEXT, UNIQUE(v1, v2))
  SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 VARCHAR(12),
  FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 1, 'hello');
INSERT INTO child VALUES (100001, 1, 'hello');
SELECT count(*) FROM child;
UPDATE parent SET v1 = NULL, v2 = 'hello2' WHERE h = 1; -- should pass even when hello2 is not in parent table.
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
UPDATE parent SET v1 = 4, v2 = 'hello' WHERE h = 1; -- this should not update child table now.
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
DROP TABLE child;
DROP TABLE parent;

-- ON UPDATE / ON DELETE NO ACTION
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE NO ACTION ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 10), (2, 20);
INSERT INTO child VALUES (100001, 10), (100002, 20);
SELECT count(*) FROM child;
UPDATE parent SET v1 = 11 WHERE h = 1; -- should fail because parent1 does not have this value.
DELETE FROM parent WHERE h = 2; -- should fail
SELECT count(*) FROM child;
BEGIN;
  UPDATE parent SET v1 = 11 WHERE h = 1; -- should pass for now
  INSERT INTO parent VALUES (3, 10);
COMMIT; -- should pass
SELECT count(*) FROM parent WHERE v1 = 11;
BEGIN;
  DELETE FROM parent WHERE h = 2; -- should pass for now
  INSERT INTO parent VALUES (4, 20);
COMMIT; -- should pass
SELECT count(*) FROM parent WHERE h = 2;
DROP TABLE child;
DROP TABLE parent;

-- ON UPDATE / ON DELETE RESTRICT
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT,
  FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE RESTRICT ON UPDATE RESTRICT DEFERRABLE INITIALLY DEFERRED) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 10), (2, 20);
INSERT INTO child VALUES (100001, 10), (100002, 20);
SELECT count(*) FROM child;
UPDATE parent SET v1 = 11 WHERE h = 1; -- should fail because parent1 does not have this value.
DELETE FROM parent WHERE h = 2; -- should fail
SELECT count(*) FROM child;
BEGIN;
  UPDATE parent SET v1 = 11 WHERE h = 1; -- should fail
  INSERT INTO parent VALUES (3, 10);
COMMIT; -- should fail
SELECT count(*) FROM parent WHERE v1 = 11;
BEGIN;
  DELETE FROM parent WHERE h = 2; -- should fail
  INSERT INTO parent VALUES (4, 20);
COMMIT; -- should fail
SELECT count(*) FROM parent WHERE h = 2;
DROP TABLE child;
DROP TABLE parent;

-- ON UPDATE / ON DELETE SET NULL
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(12), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE SET NULL ON UPDATE SET NULL) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'hello'), (2, 'world');
INSERT INTO child VALUES (100001, 'hello'), (100002, 'world');
SELECT count(*) FROM child;
UPDATE parent SET v1 = 'hello2' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child WHERE h = 100002 AND v1 IS NULL;
DROP TABLE child;
DROP TABLE parent;

-- ON UPDATE / ON DELETE SET DEFAULT
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 TEXT NOT NULL, UNIQUE(v1, v2))
 SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT DEFAULT 1, v2 VARCHAR(12) DEFAULT 'default',
  FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  ON DELETE SET DEFAULT ON UPDATE SET DEFAULT) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 1, 'hello'), (2, 70, 'world'), (3, 80, 'yugabyte');
INSERT INTO child VALUES (100001, 70, 'world'), (100002, 80, 'yugabyte');
SELECT count(*) FROM child;
-- should fail because (1, 'default') is not in parent table.
UPDATE parent SET v1 = 71, v2 = 'world2' WHERE h = 2;
INSERT INTO parent VALUES (4, 1, 'default');
UPDATE parent SET v1 = 71, v2 = 'world2' WHERE h = 2; -- should pass now
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 71 AND v2 = 'world2';
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 1 AND v2 = 'default';
DELETE FROM parent WHERE h = 3;
SELECT count(*) FROM child WHERE v1 = 1 AND v2 = 'default';
DROP TABLE child;
DROP TABLE parent;

-- Tests for partitioned table
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
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
INSERT INTO child VALUES (120, 999); -- should fail: no matching parent row for FK value.
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
INSERT INTO child VALUES (120, 100, 'alpha2'); -- should fail: no matching parent row for FK value.
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
INSERT INTO child VALUES (190, 100, 'alpha2'); -- should fail: no matching parent row for FK value.
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
INSERT INTO child VALUES (10, 10000); -- should fail: no matching parent row for FK value.
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
INSERT INTO child VALUES (100010, 120, 300, 9); -- should fail: no matching parent row for FK value.
UPDATE parent SET v1 = 101 WHERE h = 10;
SELECT count(*) FROM child WHERE id IN (100001, 100004) AND v1 = 101;
DELETE FROM parent WHERE h = 120;
SELECT count(*) FROM child WHERE h = 120;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;

-- Composite FK MATCH SIMPLE (explicit)
-- partial-null parent update still cascades with ON UPDATE CASCADE.
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 TEXT, UNIQUE(v1, v2));
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 VARCHAR(12), FOREIGN KEY(v1, v2)
  REFERENCES parent(v1, v2) MATCH SIMPLE ON DELETE CASCADE ON UPDATE CASCADE);
INSERT INTO parent VALUES (1, 1, 'hello');
INSERT INTO child VALUES (100001, 1, 'hello');
SELECT count(*) FROM child;
UPDATE parent SET v1 = NULL, v2 = 'hello2' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
UPDATE parent SET v1 = 4, v2 = 'hello' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
DROP TABLE child;
DROP TABLE parent;

-- Composite FK MATCH FULL: cascaded UPDATE would set mixed null/non-null FK columns -- not allowed.
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT, v2 TEXT, UNIQUE(v1, v2));
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, v2 VARCHAR(12), FOREIGN KEY(v1, v2)
  REFERENCES parent(v1, v2) MATCH FULL ON DELETE CASCADE ON UPDATE CASCADE);
INSERT INTO parent VALUES (1, 1, 'hello');
INSERT INTO child VALUES (100001, 1, 'hello');
SELECT count(*) FROM child;
UPDATE parent SET v1 = NULL, v2 = 'hello2' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = 1 AND v2 = 'hello';
DROP TABLE child;
DROP TABLE parent;

-- yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = false
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 SMALLINT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
SET yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = false;
\set initial_v1 456
\set updated_v1 457
\i yb_commands/foreign_key_when_types_mismatch_helper.sql
SET yb_enable_fkey_batched_docdb_lookup_when_types_mismatch = true;

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
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
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
CREATE TABLE parent(h INT PRIMARY KEY, v1 INT UNIQUE) SPLIT INTO 1 TABLETS;
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

-- COPY: int4 -> int8 FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 BIGINT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 INT, FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 100), (2, 200);
COPY child FROM stdin;
100001	100
100002	200
\.
SELECT count(*) FROM child;
INSERT INTO child VALUES (100003, 999); -- should fail: no matching parent row
DROP TABLE child;
DROP TABLE parent;

-- INSERT ... ON CONFLICT: varchar -> text FK mismatch.
CREATE TABLE parent(h INT PRIMARY KEY, v1 TEXT UNIQUE) SPLIT INTO 1 TABLETS;
CREATE TABLE child(h INT PRIMARY KEY, v1 VARCHAR(12), FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma');
INSERT INTO child VALUES (100001, 'alpha'), (100002, 'beta');
INSERT INTO child VALUES (100001, 'alpha') ON CONFLICT (h) DO UPDATE SET v1 = 'gamma';
SELECT * from child;
INSERT INTO child VALUES (100001, 'beta') ON CONFLICT (h) DO UPDATE SET v1 = 'delta';
SELECT * from child;
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
SELECT * FROM child;
INSERT INTO child VALUES (50, 99); -- should fail: no matching parent key
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
SELECT * FROM child;
INSERT INTO child VALUES (122, 'missing'); -- should fail: no matching parent key
-- Attach partition whose rows violate the FK (validation should fail).
CREATE TABLE child_p_bad(id INT PRIMARY KEY, rk VARCHAR(12)) SPLIT INTO 1 TABLETS;
INSERT INTO child_p_bad VALUES (201, 'missing');
ALTER TABLE child ATTACH PARTITION child_p_bad FOR VALUES FROM (200) TO (300); -- should fail
DROP TABLE child_p_bad;
DROP TABLE child_p_good;
DROP TABLE child_p1;
DROP TABLE child;
DROP TABLE parent;

-- Self-referential FK: int4 -> int8 (ref_id references id).
CREATE TABLE parent(id INT8 PRIMARY KEY, ref_id INT4 REFERENCES parent(id)) SPLIT INTO 1 TABLETS;
INSERT INTO parent VALUES (1, NULL);
INSERT INTO parent VALUES (2, 1);
INSERT INTO parent VALUES (3, 2);
SELECT * FROM parent;
INSERT INTO parent VALUES (4, 999); -- should fail: no id=999
INSERT INTO parent VALUES (5, 5);
DROP TABLE parent;
