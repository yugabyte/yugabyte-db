CREATE TABLE p1 (k INT PRIMARY KEY);
CREATE TABLE p2 (k INT PRIMARY KEY);
CREATE TABLE t_test (k INT PRIMARY KEY,
					 p1_fk INT REFERENCES p1(k),
					 p2_fk INT REFERENCES p2(k) DEFERRABLE INITIALLY DEFERRED);

-- Load some data into each of the tables
INSERT INTO p1 (SELECT generate_series(1, 15));
INSERT INTO p2 (SELECT generate_series(1, 15));

-- Commit stats are not displayed by default
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_test VALUES (1, 1, 1);

-- When enabled, commit stats should include read requests indicating the deferred foreign key check
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (2, 2, 2);

-- Commit stats should not be shown if EXPLAIN is invoked within an explicit transaction
BEGIN;
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (3, 3, 3);
ROLLBACK;

-- Commit stats should be displayed for the last statement in a multi-statement query
-- when autocommit is turned on. This is only applicable to the simple query protocol
-- which is used by the regress tests.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (4, 4, 4) \; EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (5, 5, 5);

\set AUTOCOMMIT OFF
-- However, when autocommit is turned off, commit stats should not be displayed as nothing is committed.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (6, 6, 6) \; EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (7, 7, 7);
ROLLBACK;
\set AUTOCOMMIT ON

-- Commit stats should not be displayed if there is nothing to display.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO p1 VALUES (101);

-- Commit stats should respect the other options provided to EXPLAIN. No commit stats should be
-- displayed for any of the following queries.
EXPLAIN (COSTS OFF) INSERT INTO t_test VALUES (8, 8, 8);
EXPLAIN (COMMIT, COSTS OFF) INSERT INTO t_test VALUES (8, 8, 8);
EXPLAIN (ANALYZE, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (9, 9, 9);
EXPLAIN (ANALYZE, DIST, COMMIT, SUMMARY OFF, COSTS OFF) INSERT INTO t_test VALUES (10, 10, 10);

-- Commit stats should also respect the format option.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF, FORMAT JSON) INSERT INTO t_test VALUES (11, 11, 11);

SELECT * FROM t_test ORDER BY k;

-- DEBUG without DIST should produce a warning and no debug metrics
EXPLAIN (ANALYZE, DEBUG, COSTS OFF) SELECT * FROM p1 WHERE k = 1;

-- DEBUG with yb_explain_hide_non_deterministic_fields should produce a warning and no debug metrics
SHOW yb_explain_hide_non_deterministic_fields;
EXPLAIN (ANALYZE, DEBUG, DIST, COSTS OFF) SELECT * FROM p1 WHERE k = 1;


-- #30768 Check duplicate fields
SET client_min_messages TO 'warning';

SET yb_explain_hide_non_deterministic_fields = off;
SET yb_enable_cbo = on;

DROP FUNCTION IF EXISTS get_duplicate_json_keys CASCADE;
CREATE FUNCTION get_duplicate_json_keys(j json)
RETURNS SETOF text AS $$
DECLARE
    v json;
BEGIN
    -- 1. If it's an object, check for duplicates at the current level
    IF json_typeof(j) = 'object' THEN
        RETURN QUERY
            SELECT keys
            FROM json_object_keys(j) AS keys
            GROUP BY keys
            HAVING COUNT(*) > 1;

        -- 2. Recurse into all nested values of the object
        FOR v IN SELECT value FROM json_each(j) LOOP
            RETURN QUERY SELECT get_duplicate_json_keys(v);
        END LOOP;

    -- 3. If it's an array, recurse into all array elements
    ELSIF json_typeof(j) = 'array' THEN
        FOR v IN SELECT value FROM json_array_elements(j) LOOP
            RETURN QUERY SELECT get_duplicate_json_keys(v);
        END LOOP;
    END IF;
END;
$$ LANGUAGE plpgsql;

RESET client_min_messages;
DO $$
DECLARE
    explain_json json;
    dup_key text;
    found_duplicates boolean := false;
BEGIN
    -- Capture the EXPLAIN (FORMAT JSON) output into our json variable.
    EXECUTE '/*+ IndexScan(p1) */'||
            'EXPLAIN (ANALYZE, DIST, DEBUG, COSTS OFF, SUMMARY OFF, FORMAT JSON)'||
            'SELECT * from p1 where k = 1'
    INTO explain_json;

    -- Look for duplicates
    FOR dup_key IN SELECT DISTINCT * FROM get_duplicate_json_keys(explain_json)
    LOOP
        found_duplicates := true;
        RAISE WARNING 'Duplicate key found in EXPLAIN output: "%"', dup_key;
    END LOOP;

    IF found_duplicates THEN
        RAISE EXCEPTION 'Test Failed: EXPLAIN output contains duplicated fields.';
    ELSE
        RAISE NOTICE 'Test Passed: No duplicated fields detected.';
    END IF;
END;
$$;

RESET yb_explain_hide_non_deterministic_fields;
RESET yb_enable_cbo;
