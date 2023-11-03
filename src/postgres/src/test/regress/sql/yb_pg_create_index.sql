--
-- CREATE_INDEX
-- Create ancillary data structures (i.e. indices)
--

--
-- BTREE
--
CREATE INDEX onek_unique1 ON onek USING btree(unique1 int4_ops ASC);

CREATE INDEX IF NOT EXISTS onek_unique1 ON onek USING btree(unique1 int4_ops ASC);

CREATE INDEX IF NOT EXISTS ON onek USING btree(unique1 int4_ops ASC);

CREATE INDEX onek_unique2 ON onek USING btree(unique2 int4_ops ASC);

CREATE INDEX onek_hundred ON onek USING btree(hundred int4_ops ASC);

CREATE INDEX onek_stringu1 ON onek USING btree(stringu1 name_ops ASC);

CREATE INDEX tenk1_unique1 ON tenk1 USING btree(unique1 int4_ops ASC);

CREATE INDEX tenk1_unique2 ON tenk1 USING btree(unique2 int4_ops ASC);

CREATE INDEX tenk1_hundred ON tenk1 USING btree(hundred int4_ops ASC);

CREATE INDEX tenk1_thous_tenthous ON tenk1 (thousand ASC, tenthous ASC);

CREATE INDEX tenk2_unique1 ON tenk2 USING btree(unique1 int4_ops ASC);

CREATE INDEX tenk2_unique2 ON tenk2 USING btree(unique2 int4_ops ASC);

CREATE INDEX tenk2_hundred ON tenk2 USING btree(hundred int4_ops ASC);

CREATE INDEX rix ON road USING btree (name text_ops ASC);

CREATE INDEX iix ON ihighway USING btree (name text_ops ASC);

CREATE INDEX six ON shighway USING btree (name text_ops ASC);

--
-- BTREE partial indices
--
CREATE INDEX onek2_u1_prtl ON onek2 USING btree(unique1 int4_ops ASC)
	where unique1 < 20 or unique1 > 980;

CREATE INDEX onek2_u2_prtl ON onek2 USING btree(unique2 int4_ops ASC)
	where stringu1 < 'B';

CREATE INDEX onek2_stu1_prtl ON onek2 USING btree(stringu1 name_ops ASC)
	where onek2.stringu1 >= 'J' and onek2.stringu1 < 'K';

--
-- GIN over int[] and text[]
--
-- Note: GIN currently supports only bitmap scans, not plain indexscans
-- YB Note: ybgin uses plain indexscans, not bitmap scans
--

SET enable_seqscan = OFF;
SET enable_indexscan = OFF;
SET enable_bitmapscan = ON;

-- YB note: the following CREATE INDEXes are commented because errdetail varies.
-- TODO(jason): uncomment when working on (issue #9959).
-- CREATE INDEX NONCONCURRENTLY intarrayidx ON array_index_op_test USING gin (i);
--
-- CREATE INDEX NONCONCURRENTLY textarrayidx ON array_index_op_test USING gin (t);

-- And try it with a multicolumn GIN index

DROP INDEX intarrayidx, textarrayidx;
-- TODO(jason): remove the following drops when working on issue #880
DROP INDEX intarrayidx;
DROP INDEX textarrayidx;

CREATE INDEX botharrayidx ON array_index_op_test USING gin (i, t);

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;

--
-- Try a GIN index with a lot of items with same key. (GIN creates a posting
-- tree when there are enough duplicates)
-- YB Note: ybgin does not use a posting list or tree
--
CREATE TABLE array_gin_test (a int[]);

INSERT INTO array_gin_test SELECT ARRAY[1, g%5, g] FROM generate_series(1, 10000) g;

CREATE INDEX array_gin_test_idx ON array_gin_test USING gin (a);

SELECT COUNT(*) FROM array_gin_test WHERE a @> '{2}';

DROP TABLE array_gin_test;

--
-- Test GIN index's reloptions
--
CREATE INDEX gin_relopts_test ON array_index_op_test USING gin (i)
  WITH (FASTUPDATE=on, GIN_PENDING_LIST_LIMIT=128);
\d+ gin_relopts_test

--
-- Try some concurrent index builds
--
-- Unfortunately this only tests about half the code paths because there are
-- no concurrent updates happening to the table at the same time.

CREATE TABLE concur_heap (f1 text, f2 text);
-- empty table
CREATE INDEX CONCURRENTLY concur_index1 ON concur_heap(f2,f1);
CREATE INDEX CONCURRENTLY IF NOT EXISTS concur_index1 ON concur_heap(f2,f1);
INSERT INTO concur_heap VALUES  ('a','b');
INSERT INTO concur_heap VALUES  ('b','b');
-- unique index
CREATE UNIQUE INDEX CONCURRENTLY concur_index2 ON concur_heap(f1);
CREATE UNIQUE INDEX CONCURRENTLY IF NOT EXISTS concur_index2 ON concur_heap(f1);
-- check if constraint is set up properly to be enforced
INSERT INTO concur_heap VALUES ('b','x');
-- check if constraint is enforced properly at build time
CREATE UNIQUE INDEX CONCURRENTLY concur_index3 ON concur_heap(f2);
-- test that expression indexes and partial indexes work concurrently
CREATE INDEX CONCURRENTLY concur_index4 on concur_heap(f2) WHERE f1='a';
CREATE INDEX CONCURRENTLY concur_index5 on concur_heap(f2) WHERE f1='x';
-- here we also check that you can default the index name
CREATE INDEX CONCURRENTLY on concur_heap((f2||f1));

-- You can't do a concurrent index build in a transaction
BEGIN;
CREATE INDEX CONCURRENTLY concur_index7 ON concur_heap(f1);
COMMIT;

-- But you can do a regular index build in a transaction
BEGIN;
CREATE INDEX std_index on concur_heap(f2);
COMMIT;

-- Failed builds are left invalid by VACUUM FULL, fixed by REINDEX
-- YB note: VACUUM and REINDEX TABLE are not yet supported
VACUUM FULL concur_heap;
REINDEX TABLE concur_heap;

--
-- REINDEX (VERBOSE)
--
CREATE TABLE reindex_verbose(id integer primary key);
\set VERBOSITY terse
REINDEX (VERBOSE) TABLE reindex_verbose;
DROP TABLE reindex_verbose;

--
-- REINDEX SCHEMA
--
REINDEX SCHEMA schema_to_reindex; -- failure, schema does not exist
