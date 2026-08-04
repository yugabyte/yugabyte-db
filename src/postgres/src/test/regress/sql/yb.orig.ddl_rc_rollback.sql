SET default_transaction_isolation = 'read committed';
-- These tests verify that any YB optimizations do not break
-- Postgres-standard behavior for procedures, exceptions, and restarts.

--------------------------------------------------------------------------------
-- TEST 1: Nested Procedures and Schema Evolution
--------------------------------------------------------------------------------
BEGIN;
CREATE TABLE proc_schema_test (id INT PRIMARY KEY);

CREATE OR REPLACE PROCEDURE alter_and_load(p_id INT, p_new_val TEXT) AS $$
BEGIN
    ALTER TABLE proc_schema_test ADD COLUMN extra_info TEXT;
    INSERT INTO proc_schema_test VALUES (p_id, p_new_val);
END;
$$ LANGUAGE plpgsql;

CALL alter_and_load(1, 'Snapshot A');

SELECT * FROM proc_schema_test;
ROLLBACK;

--------------------------------------------------------------------------------
-- TEST 2: Multi-row VALUES list with CHECK constraint failure
--------------------------------------------------------------------------------
BEGIN;
CREATE TABLE atomicity_values_test (
    id INT PRIMARY KEY,
    val INT CHECK (val > 0)
);

-- Attempt to insert 3 rows; the 3rd fails.
SAVEPOINT s1;
INSERT INTO atomicity_values_test VALUES (1, 10), (2, 20), (3, -1);
ROLLBACK TO SAVEPOINT s1;

-- Expected count: 0
SELECT count(*) AS values_test_count FROM atomicity_values_test;
ROLLBACK;

--------------------------------------------------------------------------------
-- TEST 3: Subtransaction / Exception Rollback
--------------------------------------------------------------------------------
BEGIN;
CREATE TABLE exception_test (id INT PRIMARY KEY, val TEXT);

CREATE OR REPLACE PROCEDURE test_exception_rollback() AS $$
BEGIN
    INSERT INTO exception_test VALUES (1, 'initial');
    BEGIN
        INSERT INTO exception_test VALUES (2, 'should be rolled back');
        RAISE EXCEPTION 'intentional error';
    EXCEPTION WHEN OTHERS THEN
        RAISE NOTICE 'Caught error: %', SQLERRM;
    END;
END;
$$ LANGUAGE plpgsql;

CALL test_exception_rollback();

-- Expected: Only ID 1 exists.
SELECT * FROM exception_test ORDER BY id;
ROLLBACK;

--------------------------------------------------------------------------------
-- TEST 4: "The Mega-Flush" Read Committed Atomicity
--------------------------------------------------------------------------------
CREATE TABLE data_source AS SELECT generate_series(1, 1000) AS val;

BEGIN;
CREATE TABLE target_table (id INT PRIMARY KEY);

-- Generate 1,000,000 rows to force multiple RPC flushes.
-- Failure occurs at the very end of the statement.
DO $$
BEGIN
    INSERT INTO target_table (id)
    SELECT
      CASE
        WHEN s1.val * 1000 + s2.val < 999999 THEN s1.val * 1000 + s2.val
        ELSE 1 / 0
      END
    FROM data_source s1, data_source s2;
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'Caught mega-flush error: %', SQLERRM;
END $$;

-- Expected 0.
SELECT count(*) AS leaked_row_count FROM target_table;

ROLLBACK;
RESET default_transaction_isolation;
