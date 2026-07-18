--
-- Internal Subtransactions & Procedure Exception Handling
--
-- An error caught by the exception handler rolls back the changes made in the
-- block body but not anything else.


-- Test abort subtransaction upon error. Execute SELECT statement after
-- each INSERT to avoid influence from operation buffering.

CREATE TABLE subtrans_foo (k int PRIMARY KEY, v int UNIQUE);

DO $body$
DECLARE
	row record;
BEGIN
	INSERT INTO subtrans_foo (k, v) VALUES (1, 42);
	SELECT * INTO row FROM subtrans_foo;
	BEGIN
		INSERT INTO subtrans_foo (k, v) VALUES (2, 42);
		SELECT * INTO row FROM subtrans_foo;
	EXCEPTION
		WHEN unique_violation THEN
			RAISE NOTICE 'unique violation 1';
	END;

	BEGIN
		INSERT INTO subtrans_foo (k, v) VALUES (3, 42);
		SELECT * INTO row FROM subtrans_foo;
	EXCEPTION
		WHEN unique_violation THEN
			RAISE NOTICE 'unique violation 2';
	END;
END;
$body$;

-- Full table scan
SELECT /*+ SeqScan(t) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
EXPLAIN (COSTS OFF)
SELECT /*+ SeqScan(t) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;

-- PK index scan
SELECT /*+ IndexScan(t subtrans_foo_pkey) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
EXPLAIN (COSTS OFF)
SELECT /*+ IndexScan(t subtrans_foo_pkey) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;

-- Index scan using the secondary index that triggered the unique violation
SELECT /*+ IndexScan(t subtrans_foo_v_key) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
EXPLAIN (COSTS OFF)
SELECT /*+ IndexScan(t subtrans_foo_v_key) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;


-- Test that each buffering eligible operations are executed in the intended
-- procedure block section.

CREATE TABLE subtrans_test (k int PRIMARY KEY, v text NOT NULL);

-- Single block
INSERT INTO subtrans_test (k, v) VALUES (1, 'dog');
DO $body$
BEGIN
	INSERT INTO subtrans_test (k, v) VALUES (2, 'cat');
	INSERT INTO subtrans_test (k, v) VALUES (1, 'frog'); -- error
	INSERT INTO subtrans_test (k, v) VALUES (3, 'bull');
EXCEPTION WHEN unique_violation THEN
	RAISE NOTICE 'caught unique violation';
END;
$body$;
SELECT * FROM subtrans_test ORDER BY 1;


-- Nested block combinations

-- Erroneous statement then error-free inner block
TRUNCATE TABLE subtrans_test;
INSERT INTO subtrans_test (k, v) VALUES (1, 'dog');
DO $body$
BEGIN
	INSERT INTO subtrans_test (k, v) VALUES (2, 'cat');
	INSERT INTO subtrans_test (k, v) VALUES (1, 'frog'); -- error
	BEGIN
		INSERT INTO subtrans_test (k, v) VALUES (3, 'bull');
	EXCEPTION WHEN unique_violation THEN -- force subtransaction creation
		RAISE NOTICE '*** FAILED ***';
	END;
	INSERT INTO subtrans_test (k, v) VALUES (4, 'bear');
EXCEPTION WHEN unique_violation THEN
	RAISE NOTICE 'caught unique violation';
END;
$body$;
SELECT * FROM subtrans_test ORDER BY 1;


-- Error-free inner block then erroneous statement
TRUNCATE TABLE subtrans_test;
INSERT INTO subtrans_test (k, v) VALUES (1, 'dog');
DO $body$
BEGIN
	INSERT INTO subtrans_test (k, v) VALUES (2, 'cat');
	BEGIN
		INSERT INTO subtrans_test (k, v) VALUES (3, 'bull');
	EXCEPTION WHEN unique_violation THEN -- force subtransaction creation
		RAISE NOTICE '*** FAILED ***';
	END;
	INSERT INTO subtrans_test (k, v) VALUES (4, 'bear');
	INSERT INTO subtrans_test (k, v) VALUES (1, 'frog'); -- error
EXCEPTION WHEN unique_violation THEN
	RAISE NOTICE 'caught unique violation';
END;
$body$;
SELECT * FROM subtrans_test ORDER BY 1;


-- Erroneous statement in the inner block
TRUNCATE TABLE subtrans_test;
INSERT INTO subtrans_test (k, v) VALUES (1, 'dog');
DO $body$
BEGIN
	INSERT INTO subtrans_test (k, v) VALUES (2, 'cat');
	BEGIN
		INSERT INTO subtrans_test (k, v) VALUES (3, 'bull');
		INSERT INTO subtrans_test (k, v) VALUES (1, 'frog'); -- error
	EXCEPTION WHEN unique_violation THEN
		RAISE NOTICE 'caught unique violation';
	END;
	INSERT INTO subtrans_test (k, v) VALUES (4, 'bear');
EXCEPTION WHEN unique_violation THEN
	RAISE NOTICE '*** FAILED ***';
END;
$body$;
SELECT * FROM subtrans_test ORDER BY 1;


-- Erroneous statement in the inner block, caught by the outer block handler
TRUNCATE TABLE subtrans_test;
INSERT INTO subtrans_test (k, v) VALUES (1, 'dog');
DO $body$
BEGIN
	INSERT INTO subtrans_test (k, v) VALUES (2, 'cat');
	BEGIN
		INSERT INTO subtrans_test (k, v) VALUES (3, 'bull');
		INSERT INTO subtrans_test (k, v) VALUES (1, 'frog'); -- error
	EXCEPTION WHEN not_null_violation THEN -- no matching handler
		RAISE NOTICE '*** FAILED ***';
	END;
	INSERT INTO subtrans_test (k, v) VALUES (4, 'bear');
EXCEPTION WHEN unique_violation THEN
	RAISE NOTICE 'caught unique violation';
END;
$body$;
SELECT * FROM subtrans_test ORDER BY 1;


-- Test calling a stored function from DDL statement

CREATE OR REPLACE FUNCTION inverse(float8) RETURNS float8 AS
$$
BEGIN
	RETURN 1::float8 / $1;
EXCEPTION WHEN others THEN
	RAISE NOTICE 'caught exception % %', sqlstate, sqlerrm;
        RETURN 'NaN';
END$$ LANGUAGE plpgsql;

CREATE TABLE ddl_subtrans_test (c1 float8);
INSERT INTO ddl_subtrans_test VALUES (0);
ALTER TABLE ddl_subtrans_test ADD CONSTRAINT c1_not_nan CHECK (inverse(c1) <> 'NaN');
-- Verify that the check constraint has not been added
\d ddl_subtrans_test
DROP TABLE ddl_subtrans_test;
