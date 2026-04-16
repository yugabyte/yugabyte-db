--
-- CREATE_INDEX
-- Create ancillary data structures (i.e. indices)
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

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

CREATE TABLE slow_emp4000 (
	home_base	 box
);

CREATE TABLE fast_emp4000 (
	home_base	 box
);

\set filename :abs_srcdir '/data/rect.data'
COPY slow_emp4000 FROM :'filename';

INSERT INTO fast_emp4000 SELECT * FROM slow_emp4000;

ANALYZE slow_emp4000;
ANALYZE fast_emp4000;

--
-- GIN over int[] and text[]
--
-- Note: GIN currently supports only bitmap scans, not plain indexscans
-- YB Note: ybgin uses plain indexscans, not bitmap scans
--

CREATE TABLE array_index_op_test (
	seqno		int4,
	i			int4[],
	t			text[]
);

\set filename :abs_srcdir '/data/array.data'
COPY array_index_op_test FROM :'filename';
ANALYZE array_index_op_test;

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
-- Test unique null behavior
--
CREATE TABLE unique_tbl (i int, t text);

CREATE UNIQUE INDEX unique_idx1 ON unique_tbl (i) NULLS DISTINCT;
CREATE UNIQUE INDEX unique_idx2 ON unique_tbl (i) NULLS NOT DISTINCT;

INSERT INTO unique_tbl VALUES (1, 'one');
INSERT INTO unique_tbl VALUES (2, 'two');
INSERT INTO unique_tbl VALUES (3, 'three');
INSERT INTO unique_tbl VALUES (4, 'four');
INSERT INTO unique_tbl VALUES (5, 'one');
INSERT INTO unique_tbl (t) VALUES ('six');
INSERT INTO unique_tbl (t) VALUES ('seven');  -- error from unique_idx2

DROP INDEX unique_idx1, unique_idx2;
DROP INDEX unique_idx1;
DROP INDEX unique_idx2;

INSERT INTO unique_tbl (t) VALUES ('seven');

-- build indexes on filled table
CREATE UNIQUE INDEX unique_idx3 ON unique_tbl (i) NULLS DISTINCT;  -- ok
CREATE UNIQUE INDEX unique_idx4 ON unique_tbl (i) NULLS NOT DISTINCT;  -- error
DROP INDEX unique_idx4;

DELETE FROM unique_tbl WHERE t = 'seven';

CREATE UNIQUE INDEX unique_idx4 ON unique_tbl (i) NULLS NOT DISTINCT;  -- ok now

\d unique_tbl
\d unique_idx3
\d unique_idx4
SELECT pg_get_indexdef('unique_idx3'::regclass);
SELECT pg_get_indexdef('unique_idx4'::regclass);

DROP TABLE unique_tbl;

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
-- Test ADD CONSTRAINT USING INDEX
--

CREATE TABLE cwi_test( a int , b varchar(10), c char);

-- add some data so that all tests have something to work with.

INSERT INTO cwi_test VALUES(1, 2), (3, 4), (5, 6);

CREATE UNIQUE INDEX cwi_uniq_idx ON cwi_test(a , b);
ALTER TABLE cwi_test ADD primary key USING INDEX cwi_uniq_idx;

\d cwi_test
\d cwi_uniq_idx

CREATE UNIQUE INDEX cwi_uniq2_idx ON cwi_test(b , a);
ALTER TABLE cwi_test DROP CONSTRAINT cwi_uniq_idx,
	ADD CONSTRAINT cwi_replaced_pkey PRIMARY KEY
		USING INDEX cwi_uniq2_idx;

\d cwi_test
\d cwi_replaced_pkey

DROP INDEX cwi_replaced_pkey;	-- Should fail; a constraint depends on it

DROP TABLE cwi_test;

-- ADD CONSTRAINT USING INDEX is forbidden on partitioned tables
CREATE TABLE cwi_test(a int) PARTITION BY hash (a);
create unique index on cwi_test (a);
alter table cwi_test add primary key using index cwi_test_a_idx ;
DROP TABLE cwi_test;

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
