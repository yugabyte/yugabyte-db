-- Lookups by ybctid on a table with many tablets must return the same rows as on a single-tablet
-- table: primary key point reads, IN lists spanning several tablets, and secondary index scans
-- that fetch more than one batch of ybctids, both ordered and unordered.

CREATE TABLE many_tablets(k int PRIMARY KEY, v int, s text) SPLIT INTO 16 TABLETS;
CREATE TABLE one_tablet(k int PRIMARY KEY, v int, s text) SPLIT INTO 1 TABLETS;
INSERT INTO many_tablets SELECT i, 3000 - i, 's' || i FROM generate_series(1, 3000) i;
INSERT INTO one_tablet SELECT * FROM many_tablets;
CREATE INDEX many_tablets_v_idx ON many_tablets(v ASC);
CREATE INDEX one_tablet_v_idx ON one_tablet(v ASC);

-- Primary key point read.
EXPLAIN (COSTS OFF) SELECT * FROM many_tablets WHERE k = 42;
SELECT * FROM many_tablets WHERE k = 42;
SELECT * FROM many_tablets WHERE k = 3001;

-- IN list on the primary key, hitting several tablets.
EXPLAIN (COSTS OFF) SELECT * FROM many_tablets WHERE k IN (1, 2, 500, 1500, 2999, 3001) ORDER BY k;
SELECT * FROM many_tablets WHERE k IN (1, 2, 500, 1500, 2999, 3001) ORDER BY k;

-- Secondary index lookups of a few rows, hitting several tablets.
EXPLAIN (COSTS OFF) /*+ IndexScan(t many_tablets_v_idx) */
SELECT * FROM many_tablets t WHERE v IN (7, 700, 1400, 2100, 2800);
/*+ IndexScan(t many_tablets_v_idx) */
SELECT * FROM many_tablets t WHERE v IN (7, 700, 1400, 2100, 2800) ORDER BY k;

-- Secondary index scans fetching more than one batch of ybctids. The index order differs from the
-- primary key order, so ordered results come from merging the per-tablet responses.
\set same_order_query '/*+ IndexScan(m many_tablets_v_idx) IndexScan(o one_tablet_v_idx) */ SELECT (SELECT string_agg(k::text, \',\') FROM (SELECT k FROM many_tablets m WHERE v > 100 ORDER BY v) ms) = (SELECT string_agg(k::text, \',\') FROM (SELECT k FROM one_tablet o WHERE v > 100 ORDER BY v) os) AS same_order;'
EXPLAIN (COSTS OFF) :same_order_query
:same_order_query
\set same_rows_query '/*+ IndexScan(m many_tablets_v_idx) IndexScan(o one_tablet_v_idx) */ (SELECT k, s FROM many_tablets m WHERE v > 100) EXCEPT ALL (SELECT k, s FROM one_tablet o WHERE v > 100);'
EXPLAIN (COSTS OFF) :same_rows_query
:same_rows_query
/*+ IndexScan(t many_tablets_v_idx) */
SELECT count(*), sum(k), count(DISTINCT s) FROM many_tablets t WHERE v > 100;

-- A smaller fetch limit makes the scan fetch many batches of ybctids.
SET yb_fetch_row_limit = 100;
:same_order_query
:same_rows_query
RESET yb_fetch_row_limit;

-- ANALYZE fetches the sampled rows by ybctid, preserving the sample order.
ANALYZE many_tablets;
SELECT reltuples FROM pg_class WHERE relname = 'many_tablets';

DROP TABLE many_tablets;
DROP TABLE one_tablet;
