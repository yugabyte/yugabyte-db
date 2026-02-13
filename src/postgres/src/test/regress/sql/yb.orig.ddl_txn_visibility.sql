-- -----------------------------------------------------------------------------
-- TEST 1: Multi-Statement Read-Your-Own-Write (RYOW)
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE ryow_test (id INT PRIMARY KEY, val TEXT);

INSERT INTO ryow_test SELECT i, 'init' FROM generate_series(1, 100) i;

-- If in_txn_limit_ht is not updated by Statement 1, Statement 2 might
-- use a read-time that is logically BEFORE Statement 1's write-time.
SELECT count(*) FROM ryow_test;

-- Statement 3: Update based on previous write
UPDATE ryow_test SET val = 'updated' WHERE id = 50;

-- Verification
SELECT id, val FROM ryow_test WHERE id = 50;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 2: Self-Referencing Insert (INSERT INTO ... SELECT FROM self)
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE self_insert_test (id INT PRIMARY KEY);

INSERT INTO self_insert_test SELECT i FROM generate_series(1, 10) i;

-- The SELECT must find the first 10 rows in the Regular DB.
-- The INSERT then writes 10 new rows (ID 11-20) to the Regular DB.
-- This must NOT loop indefinitely.
INSERT INTO self_insert_test SELECT id + 10 FROM self_insert_test;

-- Final count should be exactly 20.
SELECT count(*) FROM self_insert_test;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 3: Cross-Statement Visibility with Mixed Operations
-- -----------------------------------------------------------------------------
BEGIN;
CREATE TABLE mixed_visibility_test (id INT PRIMARY KEY, balance INT);

-- First batch
INSERT INTO mixed_visibility_test VALUES (1, 100), (2, 200);

-- Use a statement that references the data to perform a calculation
INSERT INTO mixed_visibility_test
SELECT 3, sum(balance) FROM mixed_visibility_test;

SELECT * FROM mixed_visibility_test ORDER BY id;
COMMIT;

