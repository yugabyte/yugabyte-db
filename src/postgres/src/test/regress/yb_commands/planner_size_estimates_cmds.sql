EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_10000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_1000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_100;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val1 FROM s_10000 WHERE val1 <= 4000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val FROM s_1000  WHERE val <= 4000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val FROM s_100   WHERE val <= 4000;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val1 FROM s_10000 WHERE val1 <= 40;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val FROM s_1000  WHERE val <= 40;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val FROM s_100   WHERE val <= 40;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val1 FROM s_10000 WHERE val1 <= 40 AND val1 > 20;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT val1 FROM s_10000 WHERE val1 <= 40 AND val1 > 20 AND val1 > 10;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_10000 WHERE id <= 4000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_1000  WHERE id <= 4000;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_100   WHERE id <= 4000;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_10000 WHERE id <= 40;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_1000  WHERE id <= 40;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_100   WHERE id <= 40;

EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_10000 WHERE id <= 40 AND id > 20;
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT id FROM s_10000 WHERE id <= 40 AND id > 20 AND id > 10;


CREATE INDEX s_10000_val1 on s_10000(val1);
CREATE INDEX s_10000_val2 on s_10000(val2);
CREATE INDEX s_10000_val3 on s_10000(val3);

-- PK index should prioritize primary key index in YB if selectivity is same for both indexes.
-- This is because PK index in YB is always an IndexOnlyScan whereas secondary index could be an
-- IndexScan and not an Index only scan if necessary columns are not included.
-- a. SINGLE ROW SELECTIVITY
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_10000 WHERE id = 2200 AND r = 2200 AND val2 = 200;
-- b. SINGLE KEY SELECTIVITY
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_10000 WHERE id = 2200 AND val2 = 200;

-- When you have multiple secondary indexes, the default cost model chooses the index that first
-- present in the list of qualifiers as a part of the query. This is because, both the indexes have
-- selectivity values equal to HASH SCAN SELECTIVITY.  However, we should choose the index that is
-- the best.
EXPLAIN (ANALYZE, SUMMARY off, TIMING off) SELECT * FROM s_10000 WHERE val3 = 20 AND val2 = 220;

DROP INDEX s_10000_val1;
DROP INDEX s_10000_val2;
DROP INDEX s_10000_val3;

-- We have to make sure that selectivity does not apply for non YB tables (eg. foreign tables)
CREATE EXTENSION file_fdw;
-- Use the per-run output directory: a COPY to a shared path like /tmp/tmp.csv is truncated by
-- concurrently running test instances, making the file size reported below flaky.
\getenv abs_builddir PG_ABS_BUILDDIR
\set csv_file :abs_builddir '/results/planner_size_estimates.csv'
COPY (SELECT * FROM s_100) TO :'csv_file' DELIMITER ',';
CREATE SERVER file_fdw_test_srv FOREIGN DATA WRAPPER file_fdw;
CREATE FOREIGN TABLE temp_foreign (
    id INT,
    val INT
) SERVER file_fdw_test_srv
OPTIONS (format 'csv', filename :'csv_file', header 'TRUE');
-- Hides the run-specific directory of the foreign file.
CREATE FUNCTION explain_filter(text) RETURNS SETOF text
LANGUAGE plpgsql AS $$
DECLARE
    ln text;
BEGIN
    FOR ln IN EXECUTE $1 LOOP
        RETURN NEXT regexp_replace(ln, 'Foreign File: .*/', 'Foreign File: .../');
    END LOOP;
END;
$$;
SELECT explain_filter('EXPLAIN SELECT * FROM temp_foreign');
DROP FUNCTION explain_filter(text);
DROP FOREIGN TABLE temp_foreign;
DROP SERVER file_fdw_test_srv CASCADE;
DROP EXTENSION file_fdw;
