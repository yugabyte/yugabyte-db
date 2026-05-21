-- Test isolation and atomicity for DDL and DML combined in a transaction block.
-- This suite ensures that objects created within a transaction are handled correctly
-- by the catalog and storage layers during various manipulation scenarios.

-- BASIC CRUD ON NEWLY CREATED TABLE
-- Ensure we can perform all DML operations on a table that doesn't yet exist globally.
BEGIN;
CREATE TABLE txn_isolation_test (id INT PRIMARY KEY, val TEXT, v2 INT);
INSERT INTO txn_isolation_test VALUES (1, 'initial', 10);

-- Verify internal visibility: Should see 1 row
SELECT * FROM txn_isolation_test;

UPDATE txn_isolation_test SET val = 'updated' WHERE id = 1;
-- Verify update: Should see 'updated'
SELECT * FROM txn_isolation_test;

DELETE FROM txn_isolation_test WHERE id = 1;
-- Verify delete: Should see 0 rows
SELECT COUNT(*) FROM txn_isolation_test;
ROLLBACK;

-- UNIQUE CONSTRAINT ENFORCEMENT
-- Verify that uniqueness is enforced within the transaction even if the index is new.
BEGIN;
CREATE TABLE txn_unique_test (id INT PRIMARY KEY, email TEXT);
CREATE UNIQUE INDEX ON txn_unique_test (email);

INSERT INTO txn_unique_test VALUES (1, 'test@yugabyte.com');

-- This update should succeed (self-overwrite)
UPDATE txn_unique_test SET email = 'test@yugabyte.com' WHERE id = 1;
SELECT * FROM txn_unique_test;

-- This should fail with a duplicate key error
-- We use a sub-transaction (anonymous block) or just let it fail and rollback.
INSERT INTO txn_unique_test VALUES (2, 'test@yugabyte.com');
ROLLBACK;

-- SELECT FOR UPDATE (LOCKING) ON NEW TABLE
-- Verify that locking works correctly on rows that have not yet been committed.
BEGIN;
CREATE TABLE txn_lock_test (id INT PRIMARY KEY, status TEXT);
INSERT INTO txn_lock_test VALUES (1, 'locked');

-- Should return the row and place a strong lock
SELECT * FROM txn_lock_test WHERE id = 1 FOR UPDATE;

UPDATE txn_lock_test SET status = 'processed' WHERE id = 1;
COMMIT;

-- Post-commit check
SELECT * FROM txn_lock_test;

-- SIMPLE SAVEPOINT AND ATOMICITY
-- Verify that partial rollbacks correctly restore the state of a new table.
BEGIN;
CREATE TABLE txn_savepoint_test (a INT PRIMARY KEY);
INSERT INTO txn_savepoint_test VALUES (1);

SAVEPOINT sp1;
  INSERT INTO txn_savepoint_test VALUES (2);
  INSERT INTO txn_savepoint_test VALUES (3);
  SELECT COUNT(*) FROM txn_savepoint_test; -- Should see 3
ROLLBACK TO SAVEPOINT sp1;

-- Should only see row 1
SELECT * FROM txn_savepoint_test ORDER BY a;
COMMIT;

-- IDENTITY AND SEQUENCE BEHAVIOR
-- Verify that sequences created in-transaction work with new tables.
BEGIN;
CREATE TABLE txn_identity_test (
    id SERIAL PRIMARY KEY,
    name TEXT
);
INSERT INTO txn_identity_test (name) VALUES ('Alpha'), ('Beta');
SELECT * FROM txn_identity_test ORDER BY id;
ROLLBACK;

-- DDL INTERACTION: ADDING COLUMNS AFTER INSERT
-- Verify that schema changes to a new table interact correctly with existing uncommitted data.
BEGIN;
CREATE TABLE txn_alter_test (id INT PRIMARY KEY);
INSERT INTO txn_alter_test VALUES (1);

ALTER TABLE txn_alter_test ADD COLUMN new_col TEXT DEFAULT 'default_val';
-- Should see the default value for the existing row
SELECT * FROM txn_alter_test;

INSERT INTO txn_alter_test VALUES (2, 'custom_val');
SELECT * FROM txn_alter_test ORDER BY id;
COMMIT;
