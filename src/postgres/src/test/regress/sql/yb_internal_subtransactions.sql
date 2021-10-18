--
-- Internal Subtransactions & Procedure Exception Handling
--

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
EXPLAIN (COSTS OFF) SELECT /*+ SeqScan(t) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;

-- PK index scan
SELECT /*+ IndexScan(t subtrans_foo_pkey) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
EXPLAIN (COSTS OFF) SELECT /*+ IndexScan(t subtrans_foo_pkey) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;

-- Index scan using the secondary index that triggered the unique violation
SELECT /*+ IndexScan(t subtrans_foo_v_key) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
EXPLAIN (COSTS OFF) SELECT /*+ IndexScan(t subtrans_foo_v_key) */ * FROM subtrans_foo t WHERE k > 0 AND v > 0 ORDER BY k;
