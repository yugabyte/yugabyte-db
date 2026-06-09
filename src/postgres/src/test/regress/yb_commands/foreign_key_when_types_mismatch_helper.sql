-- Helper sequence for single-column parent/child FK type mismatch cases.
-- Required psql variables:
--   initial_v1
--   updated_v1
INSERT INTO parent VALUES (1, :'initial_v1');
INSERT INTO child VALUES (1, :'initial_v1'); -- warm up catalog cache
DELETE FROM child WHERE h = 1; -- undo changes due to warm up
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (DIST, ANALYZE, COSTS OFF) INSERT INTO child VALUES (100001, :'initial_v1');
COMMIT;
SELECT count(*) FROM child;
ALTER TABLE child DROP CONSTRAINT child_v1_fkey;
ALTER TABLE child ADD CONSTRAINT child_v1_fkey FOREIGN KEY(v1) REFERENCES parent(v1)
  ON DELETE CASCADE ON UPDATE CASCADE;
UPDATE parent SET v1 = :'updated_v1' WHERE h = 1;
SELECT count(*) FROM child WHERE h = 100001 AND v1 = :'updated_v1';
DELETE FROM parent WHERE h = 1;
SELECT count(*) FROM child;
DROP TABLE child;
DROP TABLE parent;
