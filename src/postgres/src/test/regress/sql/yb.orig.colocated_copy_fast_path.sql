--
-- COPY into a colocated table with yb_fast_path_for_colocated_copy writes
-- without a distributed transaction, in batches sized for one entry per
-- secondary index per row.  If a row can get another number of index entries,
-- a batch can end between a row and its index entry, so COPY must not use the
-- fast path.  Each COPY below fails on its last row: with the fast path the
-- rows before it stay, without it none do.
--
CREATE DATABASE colo_copy_fast_path WITH COLOCATION = true;
\c colo_copy_fast_path
SET yb_fast_path_for_colocated_copy = on;
-- A batch of 2 writes holds one row and its index entry.
SET ysql_session_max_batch_size = 2;

-- Plain index: uses the fast path, and every row kept has its index entry.
CREATE TABLE plain (k INT PRIMARY KEY, v INT CHECK (v < 999));
CREATE INDEX plain_v_idx ON plain (v);
COPY plain FROM STDIN WITH (FORMAT csv);
1,1
2,2
3,999
\.
SELECT * FROM plain ORDER BY k;
SELECT yb_index_check('plain_v_idx'::regclass);

-- Partial index that row 1 doesn't match.
CREATE TABLE partial (k INT PRIMARY KEY, v INT CHECK (v < 999));
CREATE INDEX partial_v_idx ON partial (v) WHERE v > 1;
COPY partial FROM STDIN WITH (FORMAT csv);
1,1
2,2
3,999
\.
SELECT * FROM partial ORDER BY k;

-- An index not ready for inserts, on a table that also has a ready index.  A
-- CREATE INDEX that fails before marking its index ready leaves it that way.
CREATE TABLE not_ready (k INT PRIMARY KEY, v INT CHECK (v < 999));
CREATE INDEX not_ready_v_idx ON not_ready (v);
SET yb_test_fail_index_state_change = 'indisready';
CREATE INDEX not_ready_k_idx ON not_ready (k);
RESET yb_test_fail_index_state_change;
SELECT indexrelid::regclass, indisready FROM pg_index
    WHERE indrelid = 'not_ready'::regclass ORDER BY 1;
COPY not_ready FROM STDIN WITH (FORMAT csv);
1,1
2,2
3,999
\.
SELECT * FROM not_ready ORDER BY k;

-- Vector index, whose entries DocDB writes along with the row, on a table
-- that also has an lsm index.
CREATE EXTENSION vector;
CREATE TABLE vec (k INT PRIMARY KEY, v INT CHECK (v < 999), e vector(2));
CREATE INDEX vec_e_idx ON vec USING ybhnsw (e vector_l2_ops);
CREATE INDEX vec_v_idx ON vec (v);
COPY vec (k, v) FROM STDIN WITH (FORMAT csv);
1,1
2,2
3,999
\.
SELECT * FROM vec ORDER BY k;
