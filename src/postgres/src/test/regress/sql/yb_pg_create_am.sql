--
-- Create access method tests
--

-- Make gist2 over gisthandler. In fact, it would be a synonym to gist.
CREATE ACCESS METHOD gist2 TYPE INDEX HANDLER gisthandler;

-- Verify return type checks for handlers
CREATE ACCESS METHOD bogus TYPE INDEX HANDLER int4in;
CREATE ACCESS METHOD bogus TYPE INDEX HANDLER heap_tableam_handler;


-- Try to create gist2 index on fast_emp4000: fail because opclass doesn't exist
CREATE INDEX grect2ind2 ON fast_emp4000 USING gist2 (home_base); -- YB: Should fail because gist is unsupported on YB tables
CREATE TEMP TABLE fast_emp4000 AS SELECT * FROM fast_emp4000; -- YB: workaround using temp table
CREATE INDEX grect2ind2 ON fast_emp4000 USING gist2 (home_base);

-- Make operator class for boxes using gist2
CREATE OPERATOR CLASS box_ops DEFAULT
	FOR TYPE box USING gist2 AS
	OPERATOR 1	<<,
	OPERATOR 2	&<,
	OPERATOR 3	&&,
	OPERATOR 4	&>,
	OPERATOR 5	>>,
	OPERATOR 6	~=,
	OPERATOR 7	@>,
	OPERATOR 8	<@,
	OPERATOR 9	&<|,
	OPERATOR 10	<<|,
	OPERATOR 11	|>>,
	OPERATOR 12	|&>,
	FUNCTION 1	gist_box_consistent(internal, box, smallint, oid, internal),
	FUNCTION 2	gist_box_union(internal, internal),
	-- don't need compress, decompress, or fetch functions
	FUNCTION 5	gist_box_penalty(internal, internal, internal),
	FUNCTION 6	gist_box_picksplit(internal, internal),
	FUNCTION 7	gist_box_same(box, box, internal);

-- Create gist2 index on fast_emp4000
CREATE INDEX grect2ind2 ON fast_emp4000 USING gist2 (home_base);

-- Now check the results from plain indexscan; temporarily drop existing
-- index grect2ind to ensure it doesn't capture the plan
-- BEGIN; -- YB: Commented out because DDL in txn is unsupported
DROP INDEX grect2ind; -- YB: fails because CREATE INDEX grect2ind is not yet ported in yb_pg_create_index
SET enable_seqscan = OFF;
SET enable_indexscan = ON;
SET enable_bitmapscan = OFF;

EXPLAIN (COSTS OFF)
SELECT * FROM fast_emp4000
    WHERE home_base <@ '(200,200),(2000,1000)'::box
    ORDER BY (home_base[0])[0];
SELECT * FROM fast_emp4000
    WHERE home_base <@ '(200,200),(2000,1000)'::box
    ORDER BY (home_base[0])[0];

EXPLAIN (COSTS OFF)
SELECT count(*) FROM fast_emp4000 WHERE home_base && '(1000,1000,0,0)'::box;
SELECT count(*) FROM fast_emp4000 WHERE home_base && '(1000,1000,0,0)'::box;

EXPLAIN (COSTS OFF)
SELECT count(*) FROM fast_emp4000 WHERE home_base IS NULL;
SELECT count(*) FROM fast_emp4000 WHERE home_base IS NULL;

-- ROLLBACK; -- YB: We don't need to rollback as we didn't begin a transaction.

-- Try to drop access method: fail because of dependent objects
DROP ACCESS METHOD gist2;

-- Drop access method cascade
DROP ACCESS METHOD gist2 CASCADE;
