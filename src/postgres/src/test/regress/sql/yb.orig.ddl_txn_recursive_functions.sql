-- Test recursive DML and nested execution contexts on newly created tables.
-- These tests ensure that storage operations (including deletions and truncations)
-- behave correctly when both the data and the logic are uncommitted.

-- 1. UPDATE inside a function during a SELECT
-- Verifies "Read-Your-Own-Writes" consistency in a nested context.
BEGIN;

CREATE TABLE recursive_update_test (
    id INT PRIMARY KEY,
    val INT,
    status TEXT DEFAULT 'original'
);

CREATE OR REPLACE FUNCTION update_and_return(row_id INT)
RETURNS INT AS $$
BEGIN
    UPDATE recursive_update_test
    SET val = val + 10,
        status = 'modified_by_fn'
    WHERE id = row_id;

    RETURN row_id;
END;
$$ LANGUAGE plpgsql;

INSERT INTO recursive_update_test VALUES (1, 100), (2, 200);

-- Parent scan and nested update both targeting the same tablets.
SELECT id, val, update_and_return(id) FROM recursive_update_test ORDER BY id;

-- Verify modifications are visible.
SELECT * FROM recursive_update_test ORDER BY id;

ROLLBACK;

-- 2. INSERT inside a function during a SELECT
-- Verifies table growth handling during an active scan.
BEGIN;

CREATE TABLE recursive_insert_test (id INT PRIMARY KEY);

CREATE OR REPLACE FUNCTION insert_another(current_id INT)
RETURNS INT AS $$
BEGIN
    IF current_id < 10 THEN
        INSERT INTO recursive_insert_test VALUES (current_id + 10);
    END IF;
    RETURN current_id;
END;
$$ LANGUAGE plpgsql;

INSERT INTO recursive_insert_test VALUES (1);

-- Standard visibility usually prevents the current scan from seeing the new row.
SELECT id, insert_another(id) FROM recursive_insert_test;

-- Verify the row was indeed added.
SELECT * FROM recursive_insert_test ORDER BY id;

ROLLBACK;

-- 3. DELETE inside a function during a SELECT
-- Verifies that tombstones written to uncommitted tables are correctly handled.
BEGIN;

CREATE TABLE recursive_delete_test (
    id INT PRIMARY KEY,
    val TEXT
);

CREATE OR REPLACE FUNCTION delete_next_row(current_id INT)
RETURNS TEXT AS $$
BEGIN
    -- Delete the "next" row in the sequence.
    DELETE FROM recursive_delete_test WHERE id = current_id + 1;
    RETURN 'processed ' || current_id;
END;
$$ LANGUAGE plpgsql;

INSERT INTO recursive_delete_test VALUES (1, 'keep'), (2, 'delete me'), (3, 'keep');

-- When processing row 1, the function deletes row 2.
SELECT id, delete_next_row(id) FROM recursive_delete_test ORDER BY id;

-- Verify row 2 is gone.
SELECT * FROM recursive_delete_test ORDER BY id;

ROLLBACK;

-- 4. TRUNCATE inside a function
-- High-stress test: clearing storage mid-scan.
BEGIN;

CREATE TABLE truncate_recursive_test (id INT PRIMARY KEY);
INSERT INTO truncate_recursive_test SELECT generate_series(1, 10);

CREATE OR REPLACE FUNCTION nuke_table(input_id INT)
RETURNS INT AS $$
BEGIN
    -- Only truncate on the first iteration.
    IF input_id = 1 THEN
        TRUNCATE truncate_recursive_test;
    END IF;
    RETURN input_id;
END;
$$ LANGUAGE plpgsql;

-- The scan is mid-flight when the table is truncated.
SELECT id, nuke_table(id) FROM truncate_recursive_test ORDER BY id;

SELECT COUNT(*) FROM truncate_recursive_test;

ROLLBACK;
