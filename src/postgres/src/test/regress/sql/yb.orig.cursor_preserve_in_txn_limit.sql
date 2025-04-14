-- Test to ensure that the same in_txn_limit_ht is used for all FETCHes on a cursor. See
-- ReadHybridTime for information about in_txn_limit_ht.
--
-- An in_txn_limit_ht will be picked on the first FETCH performed by a cursor. The same should be
-- used for all future FETCHes on the cursor. This ensures that any data written by the same
-- transaction after the first FETCH isn't visible to the future FETCHes.
--
-- NOTE: a low ysql_prefetch_limit is needed to ensure the test is reliable i.e., it should fail if
-- the second FETCH doesn't use the same in_txn_limit as the first one. If the ysql_prefetch_limit
-- is higher than the table size, the first FETCH would end up marking the cursor scan as completed
-- and hence return no rows in the second FETCH even if a new in_txn_limit was to be used.

CREATE TABLE t1(k INT, PRIMARY KEY(k ASC)) SPLIT AT VALUES((2));
INSERT INTO t1 SELECT i FROM generate_series(1, 2) AS i;
BEGIN;
DECLARE c1 SCROLL CURSOR FOR SELECT * FROM t1;
INSERT INTO t1 VALUES(3); -- rows inserted before the first FETCH will be visible
INSERT INTO t1 VALUES(4);
FETCH 1 FROM c1;
INSERT INTO t1 VALUES(5); -- rows inserted after the first FETCH should not be visible
INSERT INTO t1 VALUES(6);
SELECT count(*) FROM t1; -- this statement uses a new in_txn_limit and hence sees all rows
FETCH 10 FROM c1;
ROLLBACK;