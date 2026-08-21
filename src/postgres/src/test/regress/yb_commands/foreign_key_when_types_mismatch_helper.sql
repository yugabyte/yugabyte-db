--
-- Helper for yb.orig.foreign_key_when_types_mismatch.sql.
--
-- Driver must CREATE parent(h, v1) and child(h, v1) with ON DELETE/UPDATE CASCADE.
-- parent(v1) must be backed by parent_v1_key with SPLIT INTO 1 TABLETS so FK batched
-- lookups hit a single tablet. Then set psql variables:
--   v1, v2, v3, v4, v5
--   trap_parent_val, trap_child_val
--
-- Each scenario inserts its own rows (parent h from 1, child h from 100001),
-- then TRUNCATEs both tables. The helper ends by dropping parent and child.
--
-- Scenarios (in order):
--   CASCADE, multi row insert, COPY, out of range insert/update,
--   SET NULL, NO ACTION deferrable (including COMMIT fail),
--   RESTRICT deferrable, partial null composite update, SET DEFAULT composite, MATCH FULL
--

-- CASCADE
INSERT INTO parent VALUES (1, :'v1');
INSERT INTO child VALUES (1, :'v1'); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, :'v1');
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = :'v3' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = :'v3';
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
INSERT INTO parent VALUES (1, :'v1');
INSERT INTO child VALUES (100002, :'v5'); -- should fail: no matching parent key
SELECT count(*) FROM child;
TRUNCATE child, parent;

-- multi row insert
INSERT INTO parent VALUES (1, :'v1'), (2, :'v2'), (3, :'v3');
INSERT INTO child VALUES (1, :'v1'); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
-- three distinct keys: run first with empty child for one batched FK read
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES
  (100007, :'v1'), (100008, :'v2'), (100009, :'v3');
COMMIT;
SELECT count(*) FROM child;
-- same key repeated
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES
  (100004, :'v1'), (100005, :'v1'), (100006, :'v1');
COMMIT;
SELECT count(*) FROM child;
-- two distinct keys with duplicate
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES
  (100001, :'v1'), (100002, :'v1'), (100003, :'v2');
COMMIT;
SELECT count(*) FROM child;
TRUNCATE child, parent;

-- COPY
INSERT INTO parent VALUES (1, :'v1'), (2, :'v2');
\set copy_cmd 'printf "100001\t' :v1 '\n100002\t' :v2 '\n"'
COPY child FROM PROGRAM :'copy_cmd';
SELECT count(*) FROM child;
\set copy_cmd 'printf "100003\t' :v1 '\n100004\t' :v2 '\n100005\t' :v5 '\n"'
COPY child FROM PROGRAM :'copy_cmd';
-- should fail: no matching parent key
SELECT count(*) FROM child;
INSERT INTO parent VALUES (3, :'trap_parent_val');
\set copy_cmd 'printf "100006\t' :trap_child_val '\n"'
COPY child FROM PROGRAM :'copy_cmd';
-- should fail: out of range
SELECT count(*) FROM child;
TRUNCATE child, parent;

-- out of range insert and parent update
INSERT INTO parent VALUES (1, :'trap_parent_val');
INSERT INTO child VALUES (100001, :'trap_child_val'); -- should fail: out of range
SELECT count(*) FROM child;
INSERT INTO parent VALUES (2, :'trap_child_val');
INSERT INTO child VALUES (100002, :'trap_parent_val');
SELECT count(*) FROM child;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child;
DELETE FROM parent WHERE h = 1;
DELETE FROM child WHERE h = 100002;
INSERT INTO parent VALUES (1, :'v1');
INSERT INTO child VALUES (100002, :'v1');
SELECT count(*) FROM child;
UPDATE parent SET v1 = :'trap_child_val' WHERE h = 1; -- should fail: cannot fit child column
SELECT count(*) FROM child WHERE h = 100002 AND v1 = :'v1';
TRUNCATE child, parent;

-- ON DELETE/UPDATE SET NULL
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE SET NULL ON UPDATE SET NULL;
INSERT INTO parent VALUES (1, :'v4'), (2, :'v5');
INSERT INTO child VALUES (100001, :'v4'), (100002, :'v5');
SELECT count(*) FROM child;
UPDATE parent SET v1 = :'v3' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL;
DELETE FROM parent WHERE h = 2;
SELECT count(*) FROM child WHERE h = 100002 AND v1 IS NULL;
TRUNCATE child, parent;

-- ON DELETE/UPDATE NO ACTION deferrable
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE NO ACTION ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED;
INSERT INTO parent VALUES (1, :'v4'), (2, :'v5');
INSERT INTO child VALUES (100001, :'v4'), (100002, :'v5');
SELECT count(*) FROM child;
UPDATE parent SET v1 = :'v3' WHERE h = 1; -- should fail: no matching parent key
DELETE FROM parent WHERE h = 2; -- should fail: no matching parent key
SELECT count(*) FROM child;
BEGIN;
  UPDATE parent SET v1 = :'v3' WHERE h = 1; -- should pass for now
  INSERT INTO parent VALUES (3, :'v4');
COMMIT; -- should pass
SELECT count(*) FROM parent WHERE v1 = :'v3';
BEGIN;
  DELETE FROM parent WHERE h = 3; -- child still references :'v4'
COMMIT; -- should fail
SELECT count(*) FROM child;
BEGIN;
  DELETE FROM parent WHERE h = 2; -- should pass for now
  INSERT INTO parent VALUES (4, :'v5');
COMMIT; -- should pass
SELECT count(*) FROM parent WHERE h = 2;
BEGIN;
  DELETE FROM parent WHERE h = 4; -- child still references :'v5'
COMMIT; -- should fail
SELECT count(*) FROM child;
TRUNCATE child, parent;

-- ON DELETE/UPDATE RESTRICT deferrable
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE RESTRICT ON UPDATE RESTRICT DEFERRABLE INITIALLY DEFERRED;
INSERT INTO parent VALUES (1, :'v4'), (2, :'v5');
INSERT INTO child VALUES (100001, :'v4'), (100002, :'v5');
SELECT count(*) FROM child;
UPDATE parent SET v1 = :'v3' WHERE h = 1; -- should fail: no matching parent key
DELETE FROM parent WHERE h = 2; -- should fail: no matching parent key
SELECT count(*) FROM child;
BEGIN;
  UPDATE parent SET v1 = :'v3' WHERE h = 1; -- should fail: no matching parent key
  INSERT INTO parent VALUES (3, :'v4');
COMMIT; -- should fail
SELECT count(*) FROM parent WHERE v1 = :'v3';
BEGIN;
  DELETE FROM parent WHERE h = 2; -- should fail: no matching parent key
  INSERT INTO parent VALUES (4, :'v5');
COMMIT; -- should fail
SELECT count(*) FROM parent WHERE h = 2;
TRUNCATE child, parent;

-- setup a composite foreign key constraint
ALTER TABLE parent ADD COLUMN v2 TEXT;
ALTER TABLE child ADD COLUMN v2 VARCHAR(12);
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
DROP INDEX parent_v1_key;
ALTER TABLE parent ADD UNIQUE (v1, v2);

-- partial null composite update
ALTER TABLE child ADD CONSTRAINT child_v1_v2_fkey FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, 1, 'hello');
INSERT INTO child VALUES (100001, 1, 'hello');
SELECT count(*) FROM child;
UPDATE parent SET v1 = NULL, v2 = 'hello2' WHERE h = 1; -- should pass: v2 need not exist in parent
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
UPDATE parent SET v1 = 4, v2 = 'hello' WHERE h = 1; -- child row should be unchanged
SELECT count(*) FROM child WHERE h = 100001 AND v1 IS NULL AND v2 = 'hello2';
TRUNCATE child, parent;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_fkey;

-- ON DELETE/UPDATE SET DEFAULT composite
ALTER TABLE child ALTER COLUMN v1 SET DEFAULT :'v1';
ALTER TABLE child ALTER COLUMN v2 SET DEFAULT 'default';
ALTER TABLE child ADD CONSTRAINT child_v1_v2_fkey FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  ON DELETE SET DEFAULT ON UPDATE SET DEFAULT;
INSERT INTO parent VALUES (1, :'v1', 'hello'), (2, :'v2', 'world'), (3, :'v4', 'yugabyte');
INSERT INTO child VALUES (100001, :'v2', 'world'), (100002, :'v4', 'yugabyte');
SELECT count(*) FROM child;
UPDATE parent SET v1 = :'v3', v2 = 'world2' WHERE h = 2; -- should fail: default pair not in parent
INSERT INTO parent VALUES (4, :'v1', 'default');
UPDATE parent SET v1 = :'v3', v2 = 'world2' WHERE h = 2; -- should pass
SELECT count(*) FROM child WHERE h = 100001 AND v1 = :'v3' AND v2 = 'world2';
SELECT count(*) FROM child WHERE h = 100001 AND v1 = :'v1' AND v2 = 'default';
DELETE FROM parent WHERE h = 3;
SELECT count(*) FROM child WHERE v1 = :'v1' AND v2 = 'default';
TRUNCATE child, parent;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_fkey;

-- MATCH FULL
ALTER TABLE child ADD CONSTRAINT child_v1_v2_fkey FOREIGN KEY(v1, v2) REFERENCES parent(v1, v2)
  MATCH FULL ON DELETE CASCADE ON UPDATE CASCADE;
INSERT INTO parent VALUES (1, :'v1', 'hello');
INSERT INTO child VALUES (100001, :'v1', 'hello');
SELECT count(*) FROM child;
INSERT INTO parent VALUES (2, NULL, 'hello');
INSERT INTO child VALUES (100002, NULL, 'hello'); -- should fail
SELECT count(*) FROM child;
INSERT INTO child VALUES (100003, NULL, NULL); -- should pass
SELECT count(*) FROM child;
UPDATE child SET v1 = NULL; -- should fail
SELECT count(*) FROM child;
UPDATE child SET v1 = NULL, v2 = NULL; -- should pass
SELECT count(*) FROM child WHERE v1 IS NULL AND v2 IS NULL;
TRUNCATE child, parent;
ALTER TABLE child DROP CONSTRAINT child_v1_v2_fkey;
ALTER TABLE parent DROP CONSTRAINT parent_v1_v2_key;
ALTER TABLE parent DROP COLUMN v2;
ALTER TABLE child DROP COLUMN v2;
CREATE UNIQUE INDEX parent_v1_key ON parent(v1) SPLIT INTO 1 TABLETS;

-- cleanup
DROP TABLE child;
DROP TABLE parent;
