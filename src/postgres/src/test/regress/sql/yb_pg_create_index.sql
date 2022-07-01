--
-- CREATE_INDEX
-- Create ancillary data structures (i.e. indices)
--

--
-- LSM
--
CREATE INDEX onek_unique1 ON onek USING lsm(unique1 int4_ops);

CREATE INDEX IF NOT EXISTS onek_unique1 ON onek USING lsm(unique1 int4_ops);

CREATE INDEX IF NOT EXISTS ON onek USING lsm(unique1 int4_ops);

CREATE INDEX onek_unique2 ON onek USING lsm(unique2 int4_ops);

CREATE INDEX onek_hundred ON onek USING lsm(hundred int4_ops);

CREATE INDEX onek_stringu1 ON onek USING lsm(stringu1 name_ops);

CREATE INDEX tenk1_unique1 ON tenk1 USING lsm(unique1 int4_ops);

CREATE INDEX tenk1_unique2 ON tenk1 USING lsm(unique2 int4_ops);

CREATE INDEX tenk1_hundred ON tenk1 USING lsm(hundred int4_ops);

CREATE INDEX tenk1_thous_tenthous ON tenk1 (thousand, tenthous);

CREATE INDEX tenk2_unique1 ON tenk2 USING lsm(unique1 int4_ops);

CREATE INDEX tenk2_unique2 ON tenk2 USING lsm(unique2 int4_ops);

CREATE INDEX tenk2_hundred ON tenk2 USING lsm(hundred int4_ops);

CREATE INDEX rix ON road USING lsm (name text_ops);

CREATE INDEX iix ON ihighway USING lsm (name text_ops);

CREATE INDEX six ON shighway USING lsm (name text_ops);

--
-- Try some concurrent index builds
--
-- Unfortunately this only tests about half the code paths because there are
-- no concurrent updates happening to the table at the same time.

CREATE TABLE concur_heap (f1 text, f2 text);
-- empty table
CREATE INDEX CONCURRENTLY concur_index1 ON concur_heap(f2,f1);

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
