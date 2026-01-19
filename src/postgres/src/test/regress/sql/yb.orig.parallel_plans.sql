SET client_min_messages = 'warning';

DROP DATABASE IF EXISTS colocated_db WITH (force);

CREATE DATABASE colocated_db WITH colocation = on;

ALTER DATABASE colocated_db SET yb_enable_optimizer_statistics = on;
ALTER DATABASE colocated_db SET yb_enable_base_scans_cost_model = on;
ALTER DATABASE colocated_db SET yb_fetch_row_limit = 0;
ALTER DATABASE colocated_db SET yb_fetch_size_limit = '1MB';
ALTER DATABASE colocated_db SET yb_parallel_range_rows = 10000;
ALTER DATABASE colocated_db SET yb_parallel_range_size = '1MB';

\c colocated_db

SET client_min_messages = 'warning';

-- Create a table with the same value appearing 100 times each in column
-- k1, k2 and k3.  Each column’s values are independently shuffled to
-- prevent correlations between columns or with the indexes.

CREATE TABLE t1m (id int, k1 int, k2 int, k3 int, v char(1536),
    PRIMARY KEY (id ASC)) WITH (COLOCATION = on);

CREATE INDEX NONCONCURRENTLY t1m_k1k2k3 ON t1m (k1 ASC, k2 ASC, k3 ASC);


-- To avoid temporary file limit error, store shuffled column values in separate
-- temp tables first then join them together.

CREATE TEMPORARY TABLE tmp1 (id int, v int, PRIMARY KEY (id));
CREATE TEMPORARY TABLE tmp2 (id int, v int, PRIMARY KEY (id));
CREATE TEMPORARY TABLE tmp3 (id int, v int, PRIMARY KEY (id));

SELECT setseed(0.777);

SET work_mem = '256MB';

-- k1, k2, k3: Each distinct value repeats 100 times each
INSERT INTO tmp1
  SELECT row_number() OVER (ORDER BY random()) AS id,
         i % (1000000 / 100) + 1 AS v FROM generate_series(1, 1000000) i;

INSERT INTO tmp2
  SELECT row_number() OVER (ORDER BY random()) AS id,
         i % (1000000 / 100) + 1 AS v FROM generate_series(1, 1000000) i;

INSERT INTO tmp3
  SELECT row_number() OVER (ORDER BY random()) AS id,
         i % (1000000 / 100) + 1 AS v FROM generate_series(1, 1000000) i;

ANALYZE tmp1, tmp2, tmp3;

/*+
  Leading(((tmp1 tmp2) tmp3))
  MergeJoin(tmp1 tmp2)
  MergeJoin(tmp1 tmp2 tmp3)
*/
INSERT INTO t1m
  SELECT row_number() OVER (), tmp1.v, tmp2.v, tmp3.v,
      lpad(sha512((tmp1.v#tmp2.v#tmp3.v)::bpchar::bytea)::bpchar, 1536, '-')
  FROM tmp1 JOIN tmp2 USING (id) JOIN tmp3 USING(id);

ANALYZE t1m;


\c colocated_db

--
-- Should choose SERIAL seq scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1 FROM t1m t;


--
-- Should choose SERIAL index scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE id BETWEEN 500000-(100/2-1) AND 500000+(100/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE id BETWEEN 500000-(400/2-1) AND 500000+(400/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE id BETWEEN 500000-(100/2-1) AND 500000+(100/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE id BETWEEN 500000-(400/2-1) AND 500000+(400/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE id BETWEEN 500000-(20000/2-1) AND 500000+(20000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE id BETWEEN 500000-(50000/2-1) AND 500000+(50000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT 0 FROM t1m t WHERE id BETWEEN 500000-(20000/2-1) AND 500000+(20000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT 0 FROM t1m t WHERE id BETWEEN 500000-(50000/2-1) AND 500000+(50000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT 0 FROM t1m t WHERE id BETWEEN 500000-(100000/2-1) AND 500000+(100000/2);


--
-- Should choose SERIAL index only scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE k1 = 5000;

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE k1 BETWEEN 5000-(4/2 - 1) AND 5000+(4/2);


--
-- Should choose SERIAL append plan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT sum(k1), sum(k2), sum(k3), avg(k1+id), avg(k2+k3)
FROM (SELECT id, k1, k2, k3 FROM t1m t1 WHERE id BETWEEN 500000-(10/2-1) AND 500000+(10/2)
      UNION ALL
      SELECT -1, k1, k2, k3 FROM t1m t2 WHERE k1 = 5000
      UNION ALL
      SELECT -2, k1, k2, k3 len FROM t1m t3 WHERE id = 100
      UNION ALL
      SELECT -3, k1, k2, k3 FROM t1m t4 WHERE k1 = 4500
) v;

EXPLAIN (COSTS off, SUMMARY off)
SELECT sum(k1), sum(k2), sum(k3), avg(k1+id), avg(k2+k3)
FROM (SELECT id, k1, k2, k3 FROM t1m t1 WHERE id BETWEEN 500000-(10/2-1) AND 500000+(10/2)
      UNION ALL
      SELECT -1, k1, k2, k3 FROM t1m t2 WHERE k1 = 5000
      UNION ALL
      SELECT -2, k1, k2, k3 len FROM t1m t3 WHERE id = 100
      UNION ALL
      SELECT -3, k1, k2, k3 FROM t1m t4 WHERE k1 <= 300
) v;


--
-- Should choose PARALLEL seq scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t;


--
-- Should choose PARALLEL index scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE k1 BETWEEN 5000-(200/2 - 1) AND 5000+(200/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE k2 BETWEEN 5000-(500/2 - 1) AND 5000+(500/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE k1 BETWEEN 5000-(1000/2 - 1) AND 5000+(1000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t WHERE k2 BETWEEN 5000-(1000/2 - 1) AND 5000+(1000/2);

EXPLAIN (COSTS off, SUMMARY off)
SELECT id, k1, k2, k3, length(v) FROM t1m t ORDER BY id;


--
-- Should choose PARALLEL index only scan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t WHERE k3 BETWEEN 5000-(1000/2 - 1) AND 5000+(1000/2);


--
-- Should choose PARALLEL append/gather plan
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT sum(k1), sum(k2), sum(k3), avg(k1+id), avg(k2+k3)
FROM (SELECT id, k1, k2, k3 FROM t1m t1 WHERE id BETWEEN 500000-(10/2-1) AND 500000+(10/2)
      UNION ALL
      SELECT -1, k1, k2, k3 FROM t1m t2 WHERE k3 = 5000
      UNION ALL
      SELECT -2, k1, k2, k3 len FROM t1m t3 WHERE id = 100
      UNION ALL
      SELECT -3, k1, k2, k3 FROM t1m t4 WHERE k3 <= 300
) v;


--
-- Should choose index only scan by default, and should be parallelized
-- with parallel_tuple_cost set to 0.01.  These queries run faster in parallel
-- but only marginally, so it's better not to parallelize them by default.
--

EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t;
/*+ Set(parallel_tuple_cost 0.01) */
EXPLAIN (COSTS off, SUMMARY off)
SELECT k1, k2, k3 FROM t1m t;

EXPLAIN (COSTS off, SUMMARY off)
SELECT 0 FROM t1m t;
/*+ Set(parallel_tuple_cost 0.01) */
EXPLAIN (COSTS off, SUMMARY off)
SELECT 0 FROM t1m t;


--
-- Correctness tests
--

/*
 * Check sanity of cost values (not NaN, +/-Inf, etc.) with very small
 * yb_parallel_range_size.
 *
 * The following function borrowed from explain.sql.
 */
-- To produce stable regression test output, it's usually necessary to
-- ignore details such as exact costs or row counts.  These filter
-- functions replace changeable output details with fixed strings.

-- To produce valid JSON output, replace numbers with "0" or "0.0" not "N"
create function explain_filter_to_json(text) returns jsonb
language plpgsql as
$$
declare
    data text := '';
    ln text;
begin
    for ln in execute $1
    loop
        -- Replace any numeric word with just '0'
        ln := regexp_replace(ln, '\m\d+\M', '0', 'g');
        data := data || ln;
    end loop;
    return data::jsonb;
end;
$$;


CREATE TABLE t10k (id int, k1 int, k2 int, k3 int, v char(1536),
    PRIMARY KEY (id ASC)) WITH (COLOCATION = on);
INSERT INTO t10k SELECT * FROM t1m WHERE id <= 10000;
-- Ensure no stats even if we start auto-analyzing in the future.
SELECT yb_reset_analyze_statistics('t10k'::regclass);

BEGIN;
  SET LOCAL yb_parallel_range_size = 10;
  SET LOCAL yb_parallel_range_rows = 1;
  SET LOCAL yb_enable_base_scans_cost_model = on;

  SET LOCAL parallel_setup_cost = 0;
  SET LOCAL parallel_tuple_cost = 0;
  SET LOCAL enable_bitmapscan = off;

  SELECT explain_filter_to_json('EXPLAIN (FORMAT json, SUMMARY off) SELECT * FROM t10k WHERE v LIKE ''Value_9'' ORDER BY k2 DESC') #> '{0, "Plan", "Total Cost"}';

ROLLBACK;


DROP TABLE t1m, t10k;
