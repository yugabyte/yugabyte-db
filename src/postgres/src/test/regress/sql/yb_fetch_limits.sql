CREATE TABLE t_large(a int primary key, v varchar(1000));
INSERT INTO t_large SELECT g, REPEAT('x', 1000) FROM generate_series(1, 5000) g;
CREATE INDEX t_large_v_idx_asc ON t_large(v ASC);

CREATE TABLE t_medium (a int primary key, v varchar(100));
INSERT INTO t_medium SELECT g, REPEAT('x', 100) FROM generate_series(1, 5000) g;
CREATE INDEX t_medium_v_idx_asc ON t_medium(v ASC);

CREATE TABLE t_small (a int primary key, v varchar(25));
INSERT INTO t_small SELECT g, REPEAT('x', 25) FROM generate_series(1, 5000) g;
CREATE INDEX t_small_v_idx_asc ON t_small(v ASC);

-- #### default limits just use the row limit ####
-- seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small;
-- YB_TODO: the yb seq scan queries' explain output differ since, for some
-- reason, yb seq scan is not being used by pg15.  Deal with this test failure
-- later on in commits 2375a8e930e4a7047c78347371352947447302e1 and
-- 465ee2c4252097b20613a60d8f1f7b59c7a29e12.
-- yb seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_large AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_medium AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_small AS t;
-- index scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small WHERE v > '';
-- index only scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_small WHERE v > '';

-- #### bounded by size limit when row limit is higher ####
SET yb_fetch_size_limit TO '1kB';
-- seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small;
-- yb seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_large AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_medium AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_small AS t;
-- index scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small WHERE v > '';
-- index only scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_small WHERE v > '';

SET yb_fetch_row_limit = 10000;
SET yb_fetch_size_limit = '1MB';
-- seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small;
-- yb seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_large AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_medium AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_small AS t;
-- index scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small WHERE v > '';
-- index only scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_small WHERE v > '';

-- #### bounded by whatever is hit first: row or size limit ####
SET yb_fetch_size_limit TO '500kB';
-- seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small;
-- yb seq scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_large AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_medium AS t;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) /*+ SeqScan(t) */ SELECT * FROM t_small AS t;
-- index scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM t_small WHERE v > '' LIMIT 1;
-- index only scan
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_large WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_medium WHERE v > '';
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT v FROM t_small WHERE v > '';

--
-- #22648: Avoid duplicate rows for index-scans where the table request does not fit into 1 response
--
create table test_correct(a int, b int, c text);
insert into test_correct select i, i, repeat('1234567890', 100) from generate_series(1, 20) i;
create index on test_correct(a asc);
set yb_fetch_size_limit = '1kB';
explain (analyze, costs off, timing off, summary off) select * from test_correct where a < 20 and b % 5 = 0;
select a, b, substring(c, 0, 10) from test_correct where a < 20 and b % 5 = 0 order by a;
