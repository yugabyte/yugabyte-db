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

-- More involved save point test case
CREATE OR REPLACE FUNCTION process_work1() RETURNS void AS $$
BEGIN
    CREATE TABLE users1 (first_name TEXT, last_name TEXT);
    CREATE TABLE audit_log1 (
        id INT PRIMARY KEY,
        msg TEXT
    );
    INSERT INTO users1 SELECT md5(random()::text), md5(random()::text) FROM
                         (SELECT * FROM generate_series(1,10000) AS id) AS id;
    INSERT INTO audit_log1 VALUES (1, 'Starting the main process...'); -- This is part of the main transaction.

    -- >>> PostgreSQL creates an internal SAVEPOINT here <<<
    BEGIN
        INSERT INTO audit_log1 VALUES (2, 'Processing a sub-task...');

        -- This next line will fail because of the primary key constraint (id=1 already exists)
        INSERT INTO audit_log1 VALUES (1, 'This will cause a unique_violation error!');

    EXCEPTION
        WHEN unique_violation THEN
            -- >>> PostgreSQL has already performed an internal ROLLBACK TO SAVEPOINT before this
            -- code runs <<<
            RAISE NOTICE 'Caught a duplicate key! The sub-task was rolled back.';
    END;
    -- >>> If the block had succeeded, an internal RELEASE SAVEPOINT would happen here <<<

    INSERT INTO audit_log1 VALUES (3, 'Main process finished.'); -- This runs because the error was handled.
END;
$$ LANGUAGE plpgsql;
-- Clear the table and run the function
SELECT process_work1();

-- Check the final state of the table
SELECT * FROM audit_log1 ORDER BY id;
SELECT COUNT(*) FROM users1;


-- create table users2 is outside begin exception block
-- insert into users2 is inside begin exception block
CREATE OR REPLACE FUNCTION process_work2() RETURNS void AS $$
BEGIN
    CREATE TABLE users2 (first_name TEXT, last_name TEXT);
    CREATE TABLE audit_log2 (
        id INT PRIMARY KEY,
        msg TEXT
    );
    INSERT INTO audit_log2 VALUES (1, 'Starting the main process...'); -- This is part of the main transaction.

    -- >>> PostgreSQL creates an internal SAVEPOINT here <<<
    BEGIN
        INSERT INTO users2 SELECT md5(random()::text), md5(random()::text) FROM
                             (SELECT * FROM generate_series(1,10000) AS id) AS id;
        INSERT INTO audit_log2 VALUES (2, 'Processing a sub-task...');

        -- This next line will fail because of the primary key constraint (id=1 already exists)
        INSERT INTO audit_log2 VALUES (1, 'This will cause a unique_violation error!');

    EXCEPTION
        WHEN unique_violation THEN
            -- >>> PostgreSQL has already performed an internal ROLLBACK TO SAVEPOINT before this
            -- code runs <<<
            RAISE NOTICE 'Caught a duplicate key! The sub-task was rolled back.';
    END;
    -- >>> If the block had succeeded, an internal RELEASE SAVEPOINT would happen here <<<

    INSERT INTO audit_log2 VALUES (3, 'Main process finished.'); -- This runs because the error was handled.
END;
$$ LANGUAGE plpgsql;
-- Clear the table and run the function
SELECT process_work2();

-- Check the final state of the table
SELECT * FROM audit_log2 ORDER BY id;
SELECT COUNT(*) FROM users2;

-- both create table users3 and insert into users3 are inside begin exception block
CREATE OR REPLACE FUNCTION process_work3() RETURNS void AS $$
BEGIN
    CREATE TABLE audit_log3 (
        id INT PRIMARY KEY,
        msg TEXT
    );
    INSERT INTO audit_log3 VALUES (1, 'Starting the main process...'); -- This is part of the main transaction.

    -- >>> PostgreSQL creates an internal SAVEPOINT here <<<
    BEGIN
        CREATE TABLE users3 (first_name TEXT, last_name TEXT);
        INSERT INTO users3 SELECT md5(random()::text), md5(random()::text) FROM
                             (SELECT * FROM generate_series(1,10000) AS id) AS id;
        INSERT INTO audit_log3 VALUES (2, 'Processing a sub-task...');

        -- This next line will fail because of the primary key constraint (id=1 already exists)
        INSERT INTO audit_log3 VALUES (1, 'This will cause a unique_violation error!');

    EXCEPTION
        WHEN unique_violation THEN
            -- >>> PostgreSQL has already performed an internal ROLLBACK TO SAVEPOINT before this
            -- code runs <<<
            RAISE NOTICE 'Caught a duplicate key! The sub-task was rolled back.';
    END;
    -- >>> If the block had succeeded, an internal RELEASE SAVEPOINT would happen here <<<

    INSERT INTO audit_log3 VALUES (3, 'Main process finished.'); -- This runs because the error was handled.
END;
$$ LANGUAGE plpgsql;
-- Clear the table and run the function
SELECT process_work3();

-- Check the final state of the table
SELECT * FROM audit_log3 ORDER BY id;
SELECT COUNT(*) FROM users3;
