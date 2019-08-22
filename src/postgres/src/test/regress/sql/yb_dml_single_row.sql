-- Regression tests for UPDATE/DELETE single row operations.

--
-- Test that single-row UPDATE/DELETEs bypass scan.
--
CREATE TABLE single_row (k int primary key, v1 int, v2 int);

-- Below statements should all USE single-row.
EXPLAIN DELETE FROM single_row WHERE k = 1;
EXPLAIN DELETE FROM single_row WHERE k = 1 RETURNING k;
EXPLAIN DELETE FROM single_row WHERE k IN (1);
-- Below statements should all NOT USE single-row.
EXPLAIN DELETE FROM single_row;
EXPLAIN DELETE FROM single_row WHERE k = 1 and v1 = 1;
EXPLAIN DELETE FROM single_row WHERE v1 = 1 and v2 = 1;
EXPLAIN DELETE FROM single_row WHERE k = 1 RETURNING v1;
EXPLAIN DELETE FROM single_row WHERE k > 1;
EXPLAIN DELETE FROM single_row WHERE k != 1;
EXPLAIN DELETE FROM single_row WHERE k IN (1, 2);

-- Below statements should all USE single-row.
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k = 1;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k = 1 RETURNING k, v1;
EXPLAIN UPDATE single_row SET v1 = 1, v2 = 1 + 2 WHERE k = 1;
EXPLAIN UPDATE single_row SET v1 = 1, v2 = 2 WHERE k = 1 RETURNING k, v1, v2;
EXPLAIN UPDATE single_row SET v1 = 1, v2 = 2 WHERE k = 1 RETURNING *;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k IN (1);
-- Below statements should all NOT USE single-row.
EXPLAIN UPDATE single_row SET v1 = 1;
EXPLAIN UPDATE single_row SET v1 = v1 + 1 WHERE k = 1;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k = 1 and v2 = 1;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k = 1 RETURNING v2;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k = 1 RETURNING *;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k > 1;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k != 1;
EXPLAIN UPDATE single_row SET v1 = 1 WHERE k IN (1, 2);

--
-- Test single-row UPDATE/DELETE execution.
--
INSERT INTO single_row VALUES (1, 1, 1);

UPDATE single_row SET v1 = 2 WHERE k = 1;
SELECT * FROM single_row;

DELETE FROM single_row WHERE k = 1;
SELECT * FROM single_row;

--
-- Test UPDATE/DELETEs of non-existent data return no rows.
--
UPDATE single_row SET v1 = 1 WHERE k = 100 RETURNING k;
DELETE FROM single_row WHERE k = 100 RETURNING k;
SELECT * FROM single_row;

--
-- Test prepared statements.
--
INSERT INTO single_row VALUES (1, 1, 1);

PREPARE single_row_update (int, int, int) AS
  UPDATE single_row SET v1 = $2, v2 = $3 WHERE k = $1;

PREPARE single_row_delete (int) AS
  DELETE FROM single_row WHERE k = $1;

EXPLAIN EXECUTE single_row_update (1, 2, 2);
EXECUTE single_row_update (1, 2, 2);
SELECT * FROM single_row;

EXPLAIN EXECUTE single_row_delete (1);
EXECUTE single_row_delete (1);
SELECT * FROM single_row;

--
-- Test returning clauses.
--
INSERT INTO single_row VALUES (1, 1, 1);

UPDATE single_row SET v1 = 2, v2 = 2 WHERE k = 1 RETURNING v1, v2, k;
SELECT * FROM single_row;

UPDATE single_row SET v1 = 3, v2 = 3 WHERE k = 1 RETURNING *;
SELECT * FROM single_row;

DELETE FROM single_row WHERE k = 1 RETURNING k;
SELECT * FROM single_row;

---
--- Test in transaction block.
---
INSERT INTO single_row VALUES (1, 1, 1);

BEGIN;
EXPLAIN DELETE FROM single_row WHERE k = 1;
DELETE FROM single_row WHERE k = 1;
END;

SELECT * FROM single_row;

-- Test UPDATE/DELETE of non-existing rows.
BEGIN;
DELETE FROM single_row WHERE k = 1;
UPDATE single_row SET v1 = 2 WHERE k = 1;
END;

SELECT * FROM single_row;

---
--- Test WITH clause.
---
INSERT INTO single_row VALUES (1, 1, 1);

EXPLAIN WITH temp AS (UPDATE single_row SET v1 = 2 WHERE k = 1)
  UPDATE single_row SET v1 = 2 WHERE k = 1;

WITH temp AS (UPDATE single_row SET v1 = 2 WHERE k = 1)
  UPDATE single_row SET v1 = 2 WHERE k = 1;

SELECT * FROM single_row;

-- Update row that doesn't exist.
WITH temp AS (UPDATE single_row SET v1 = 2 WHERE k = 2)
  UPDATE single_row SET v1 = 2 WHERE k = 2;

SELECT * FROM single_row;

-- Adding secondary index should force re-planning, which would
-- then not choose single-row plan due to the secondary index.
EXPLAIN EXECUTE single_row_delete (1);
CREATE INDEX single_row_index ON single_row (v1);
EXPLAIN EXECUTE single_row_delete (1);
DROP INDEX single_row_index;

-- Same as above but for UPDATE.
EXPLAIN EXECUTE single_row_update (1, 1, 1);
CREATE INDEX single_row_index ON single_row (v1);
EXPLAIN EXECUTE single_row_update (1, 1, 1);
DROP INDEX single_row_index;

-- Adding BEFORE DELETE row triggers should do the same as secondary index.
EXPLAIN EXECUTE single_row_delete (1);
CREATE TRIGGER single_row_delete_trigger BEFORE DELETE ON single_row
  FOR EACH ROW EXECUTE PROCEDURE suppress_redundant_updates_trigger();
EXPLAIN EXECUTE single_row_delete (1);
-- UPDATE should still use single-row since trigger does not apply to it.
EXPLAIN EXECUTE single_row_update (1, 1, 1);
DROP TRIGGER single_row_delete_trigger ON single_row;

-- Adding BEFORE UPDATE row triggers should do the same as secondary index.
EXPLAIN EXECUTE single_row_update (1, 1, 1);
CREATE TRIGGER single_row_update_trigger BEFORE UPDATE ON single_row
  FOR EACH ROW EXECUTE PROCEDURE suppress_redundant_updates_trigger();
EXPLAIN EXECUTE single_row_update (1, 1, 1);
-- DELETE should still use single-row since trigger does not apply to it.
EXPLAIN EXECUTE single_row_delete (1);
DROP TRIGGER single_row_update_trigger ON single_row;

--
-- Test table with composite primary key.
--
CREATE TABLE single_row_comp_key (v int, k1 int, k2 int, PRIMARY KEY (k1, k2));

-- Below statements should all USE single-row.
EXPLAIN DELETE FROM single_row_comp_key WHERE k1 = 1 and k2 = 1;
EXPLAIN DELETE FROM single_row_comp_key WHERE k1 = 1 and k2 = 1 RETURNING k1, k2;
-- Below statements should all NOT USE single-row.
EXPLAIN DELETE FROM single_row_comp_key;
EXPLAIN DELETE FROM single_row_comp_key WHERE k1 = 1;
EXPLAIN DELETE FROM single_row_comp_key WHERE k2 = 1;
EXPLAIN DELETE FROM single_row_comp_key WHERE v = 1 and k1 = 1 and k2 = 1;
EXPLAIN DELETE FROM single_row_comp_key WHERE k1 = 1 and k2 = 1 RETURNING v;

-- Below statements should all USE single-row.
EXPLAIN UPDATE single_row_comp_key SET v = 1 WHERE k1 = 1 and k2 = 1;
EXPLAIN UPDATE single_row_comp_key SET v = 1 WHERE k1 = 1 and k2 = 1 RETURNING k1, k2, v;
EXPLAIN UPDATE single_row_comp_key SET v = 1 + 2 WHERE k1 = 1 and k2 = 1;
-- Below statements should all NOT USE single-row.
EXPLAIN UPDATE single_row_comp_key SET v = 1;
EXPLAIN UPDATE single_row_comp_key SET v = v + 1 WHERE k1 = 1 and k2 = 1;
EXPLAIN UPDATE single_row_comp_key SET v = 1 WHERE k1 = 1 and k2 = 1 and v = 1;

-- Test execution.
INSERT INTO single_row_comp_key VALUES (1, 2, 3);

UPDATE single_row_comp_key SET v = 2 WHERE k1 = 2 and k2 = 3;
SELECT * FROM single_row_comp_key;

-- try switching around the order, reversing value/key
DELETE FROM single_row_comp_key WHERE 2 = k2 and 3 = k1;
SELECT * FROM single_row_comp_key;
DELETE FROM single_row_comp_key WHERE 3 = k2 and 2 = k1;
SELECT * FROM single_row_comp_key;

--
-- Test table with non-standard const type.
--
CREATE TABLE single_row_complex (k bigint PRIMARY KEY, v float);

-- Below statements should all USE single-row.
EXPLAIN DELETE FROM single_row_complex WHERE k = 1;
EXPLAIN DELETE FROM single_row_complex WHERE k = 1 RETURNING k;
-- Below statements should all NOT USE single-row.
EXPLAIN DELETE FROM single_row_complex;
EXPLAIN DELETE FROM single_row_complex WHERE k = 1 and v = 1;
EXPLAIN DELETE FROM single_row_complex WHERE v = 1;
EXPLAIN DELETE FROM single_row_complex WHERE k = 1 RETURNING v;

-- Below statements should all USE single-row.
EXPLAIN UPDATE single_row_complex SET v = 1 WHERE k = 1;
EXPLAIN UPDATE single_row_complex SET v = 1 WHERE k = 1 RETURNING k, v;
EXPLAIN UPDATE single_row_complex SET v = 1 + 2 WHERE k = 1;
-- Below statements should all NOT USE single-row.
EXPLAIN UPDATE single_row_complex SET v = 1;
EXPLAIN UPDATE single_row_complex SET v = v + 1 WHERE k = 1;
EXPLAIN UPDATE single_row_complex SET v = 1 WHERE k = 1 and v = 1;

-- Test execution.
INSERT INTO single_row_complex VALUES (1, 1);

UPDATE single_row_complex SET v = 2 WHERE k = 1;
SELECT * FROM single_row_complex;

DELETE FROM single_row_complex WHERE k = 1;
SELECT * FROM single_row_complex;

--
-- Test table with non-const type.
--
CREATE TABLE single_row_array (k int primary key, arr int []);

-- Below statements should all USE single-row.
EXPLAIN DELETE FROM single_row_array WHERE k = 1;
EXPLAIN DELETE FROM single_row_array WHERE k = 1 RETURNING k;
-- Below statements should all NOT USE single-row.
EXPLAIN DELETE FROM single_row_array;
EXPLAIN DELETE FROM single_row_array WHERE k = 1 and arr[1] = 1;
EXPLAIN DELETE FROM single_row_array WHERE arr[1] = 1;
EXPLAIN DELETE FROM single_row_array WHERE k = 1 RETURNING arr;

-- Below statements should all NOT USE single-row.
EXPLAIN UPDATE single_row_array SET arr[1] = 1 WHERE k = 1;

-- Test execution.
INSERT INTO single_row_array VALUES (1, ARRAY [1, 2, 3]);

DELETE FROM single_row_array WHERE k = 1;
SELECT * FROM single_row_array;

--
-- Test table without a primary key.
--
CREATE TABLE single_row_no_primary_key (a int, b int);

-- Below statements should all NOT USE single-row.
EXPLAIN DELETE FROM single_row_no_primary_key;
EXPLAIN DELETE FROM single_row_no_primary_key WHERE a = 1;
EXPLAIN DELETE FROM single_row_no_primary_key WHERE a = 1 and b = 1;

-- Below statements should all NOT USE single-row.
EXPLAIN UPDATE single_row_no_primary_key SET a = 1;
EXPLAIN UPDATE single_row_no_primary_key SET b = 1 WHERE a = 1;
EXPLAIN UPDATE single_row_no_primary_key SET b = 1 WHERE b = 1;
