\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)'

-- test yb_hash_code as a function
SELECT yb_hash_code(1,2,3);
SELECT yb_hash_code(1,2,'abc'::text);

-- test on unsupported primary key types
SELECT yb_hash_code('asdf');
SELECT yb_hash_code('{"a": {"b":{"c": "foo"}}}'::jsonb);
SELECT yb_hash_code(ARRAY[1,2,3]);

-- test basic filtering on different datatypes with yb_hash_code
CREATE TABLE test_table_int (x INT PRIMARY KEY);
INSERT INTO test_table_int SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_int;
DROP TABLE test_table_int;

CREATE TABLE test_table_real (x REAL PRIMARY KEY);
INSERT INTO test_table_real SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_real;
DROP TABLE test_table_real;

CREATE TABLE test_table_double (x DOUBLE PRECISION PRIMARY KEY);
INSERT INTO test_table_double SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_double;
DROP TABLE test_table_double;

CREATE TABLE test_table_small (x SMALLINT PRIMARY KEY);
INSERT INTO test_table_small SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_small;
DROP TABLE test_table_small;

CREATE TABLE test_table_text (x TEXT PRIMARY KEY);
INSERT INTO test_table_text SELECT generate_series(800001, 800020);
SELECT yb_hash_code(x), x FROM test_table_text;
DROP TABLE test_table_text;

CREATE TYPE mood AS ENUM ('sad', 'ok', 'happy');
SELECT yb_hash_code('sad'::mood);
SELECT yb_hash_code('happy'::mood);
CREATE TABLE test_table_mood (x mood, y INT, PRIMARY KEY((x,y) HASH));
INSERT INTO test_table_mood VALUES ('sad'::mood, 1), ('happy'::mood, 4),
('ok'::mood, 4), ('sad'::mood, 34), ('ok'::mood, 23);
SELECT yb_hash_code(x,y), * FROM test_table_mood;
DROP TABLE test_table_mood;
DROP TYPE mood;

-- test basic pushdown on a table with one primary hash key column
CREATE TABLE test_table_one_primary (x INT PRIMARY KEY, y INT);
INSERT INTO test_table_one_primary SELECT i,i FROM generate_series(1, 10000) i;
\set query ':P SELECT * FROM test_table_one_primary WHERE yb_hash_code(x) = 10427;'
\i :iter_P2
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(x) < 512;
SELECT * FROM test_table_one_primary WHERE yb_hash_code(x) < 512 LIMIT 5;
\set query ':P SELECT x, yb_hash_code(x) FROM test_table_one_primary WHERE x IN (1, 2, 3, 4) AND yb_hash_code(x) < 50000 ORDER BY x;'
\i :iter_P2
\set query ':P SELECT yb_hash_code(x) FROM test_table_one_primary WHERE yb_hash_code(x) <= 20 AND yb_hash_code(x) < 9;'
\i :iter_P2
\set query ':P SELECT yb_hash_code(x) FROM test_table_one_primary WHERE yb_hash_code(x) <= 20 AND yb_hash_code(x) > 90;'
\i :iter_P2

-- this should not be pushed down as (x,y) is not a hash primary key yet
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 512;
SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 512 LIMIT 5;

-- pushdown on yb_hash_code(x,y) should work after this index
-- on (x,y) hash is created
CREATE INDEX test_secondary ON test_table_one_primary ((x, y) HASH);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 512;
SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 512 LIMIT 5;
\set query ':P SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) = 10;'
\i :iter_P2
\set query ':P SELECT yb_hash_code(x, y) FROM test_table_one_primary WHERE yb_hash_code(x, y) <= 20 AND yb_hash_code(x, y) < 11;'
\i :iter_P2
\set query ':P SELECT yb_hash_code(x, y) FROM test_table_one_primary WHERE yb_hash_code(x, y) <= 20 AND yb_hash_code(x, y) > 110;'
\i :iter_P2


-- testing with a qualification on yb_hash_code(x) and x
\set query ':P SELECT * FROM test_table_one_primary WHERE yb_hash_code(x) < 512 AND x < 90;'
\i :iter_P2

-- should not be pushed down as the selectivity of this filter is too high
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 60000;

-- should select the more selective index on x
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) < 60000 AND yb_hash_code(x) > 60000;

-- this should not be pushed down as the order of (x,y) is not correct
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test_table_one_primary WHERE yb_hash_code(y,x) < 512;
SELECT * FROM test_table_one_primary WHERE yb_hash_code(y,x) < 512 LIMIT 7;

-- should not be pushed down as we don't support pushdown on IN filters yet
\set query ':P SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) IN (1, 200, 326);'
\i :iter_P2

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)  SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) BETWEEN 4 AND 512;
SELECT * FROM test_table_one_primary WHERE yb_hash_code(x, y) BETWEEN 4 AND 512 LIMIT 5;
DROP TABLE test_table_one_primary;

-- testing pushdown where the hash column is of type text
CREATE TABLE text_table (hr text, ti text, tj text, i int, j int, primary key (hr));
INSERT INTO text_table SELECT i::TEXT, i::TEXT, i::TEXT, i, i FROM generate_series(1,10000) i;
\set query ':P SELECT * FROM text_table WHERE yb_hash_code(hr) = 30;'
\i :iter_P2

-- testing pushdown on a secondary index with a text hash column
CREATE INDEX textidx ON text_table (tj);
\set query ':P SELECT * FROM text_table WHERE yb_hash_code(tj) = 63;'
\i :iter_P2
\set query ':P SELECT * FROM text_table WHERE yb_hash_code(tj) <= 63;'
\i :iter_P2
\set query ':P SELECT tj FROM text_table WHERE yb_hash_code(tj) <= 63;'
\i :iter_P2
\set query ':P SELECT hr FROM text_table WHERE yb_hash_code(tj) < 63;'
\i :iter_P2
\set query ':P SELECT tj FROM text_table WHERE 63 >= yb_hash_code(tj);'
\i :iter_P2
DROP TABLE text_table;

-- testing on a table with multiple hash key columns on
-- multiple types
CREATE TABLE test_table_multi_col_key(h1 BIGINT, h2 FLOAT, h3 TEXT, r1 TIMESTAMPTZ, r2 DOUBLE PRECISION, v1 INT, v2  DATE, v3 BOOLEAN, PRIMARY KEY ((h1, h2, h3) HASH, r1, r2));
INSERT INTO test_table_multi_col_key SELECT i::BIGINT, i::FLOAT, i::TEXT, '2018-12-18 04:59:54-08'::TIMESTAMPTZ, i::DOUBLE PRECISION, i::INT, '2016-06-02'::DATE,(i%2)::BOOLEAN FROM generate_series(1, 10000) i;
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60;'
\i :iter_P2

-- limit and order by
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60 LIMIT 3;'
\i :iter_P2
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60 ORDER BY h1;'
\i :iter_P2
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60 ORDER BY h1 LIMIT 3;'
\i :iter_P2

-- create an index with the same set of primary keys as
-- the primary index
CREATE INDEX multi_key_index_1 ON test_table_multi_col_key((h1,h2,h3) HASH, r1 ASC, r2 ASC);

-- create other indexes on other columsn that are not
-- hashed in the primary index
CREATE INDEX multi_key_index_2 ON test_table_multi_col_key((r1, r2, v1) HASH, v3, v2);
CREATE INDEX multi_key_index_3 ON test_table_multi_col_key((r1, r2, v2, v3) HASH, v1);

-- index only scan on multi_key_index
\set query ':P SELECT h1 from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60;'
\i :iter_P2

-- index scan on primary key index
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60;'
\i :iter_P2

-- sequential scan as the selectivity of this filter is
-- high
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 60000 LIMIT 10;'
\i :iter_P2

-- testing pushdown where the input to the yb_hash_code
-- does not match any index hash key

-- sequential scan as no index has (h1,h2,h3,v1) as the
-- hash key
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3,v1) < 60 LIMIT 10;'
\i :iter_P2

-- sequential scan as no index has (h1,h3,h2) as the
-- hash key
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h3,h2) < 60 LIMIT 10;'
\i :iter_P2

\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,v3,v2,r2) < 60 LIMIT 10;'
\i :iter_P2

\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(r2,r1,v1) < 60 LIMIT 10;'
\i :iter_P2

-- pushdown for multi_key_index_2 and multi_key_index_3
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v1) < 60 LIMIT 10;'
\i :iter_P2

\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) < 60 LIMIT 10;'
\i :iter_P2

-- cost model tests to make sure that pushdown occurs on the
-- most selective yb_hash_code filter
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) < 600 AND yb_hash_code(h1,h2,h3) < 65500 AND yb_hash_code(r1, r2, v1) > 5500;
SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) < 600 AND yb_hash_code(h1,h2,h3) < 65500 AND yb_hash_code(r1, r2, v1) > 5500 LIMIT 10;

\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) < 600 AND yb_hash_code(h1,h2,h3) > 65500 AND yb_hash_code(r1, r2, v1) > 5500;'
\i :iter_P2

-- all given filters here have very high selectivity so this
-- should be a sequential scan
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) > 600 AND yb_hash_code(h1,h2,h3) < 65500 AND yb_hash_code(r1, r2, v1) > 5500;
SELECT * from test_table_multi_col_key WHERE yb_hash_code(r1,r2,v2,v3) > 600 AND yb_hash_code(h1,h2,h3) < 65500 AND yb_hash_code(r1, r2, v1) > 5500 LIMIT 10;

-- yb_hash_code with partial hash key should not error.
-- TODO(#30756): recheck should not be needed.
\set query ':P SELECT * from test_table_multi_col_key WHERE yb_hash_code(h1,h2,h3) < 6000 AND h1 = 48;'
\i :iter_P2

DROP TABLE test_table_multi_col_key;

-- test recheck of index only scan with yb_hash_code
CREATE TABLE test_index_only_scan_recheck(k INT PRIMARY KEY, v1 INT, v2 INT, v3 INT, v4 INT);
CREATE INDEX ON test_index_only_scan_recheck(v4) INCLUDE (v1);
INSERT INTO test_index_only_scan_recheck SELECT s, s, s, s, s FROM generate_series(1, 100) AS s;
\set query ':P SELECT v1, yb_hash_code(v4) FROM test_index_only_scan_recheck WHERE v4 IN (1, 2, 3) AND yb_hash_code(v4) < 50000;'
\i :iter_P2

DROP TABLE test_index_only_scan_recheck;

-- Issue #17043
CREATE TABLE t as select x, x as y from generate_series(1, 10) x;
CREATE INDEX t_x_hash_y_asc_idx ON t (x HASH, y ASC);
\set query ':P SELECT yb_hash_code(x), y FROM t WHERE yb_hash_code(x) = 2675 AND y IN (5, 6);'
\i :iter_P2
DROP TABLE t;

CREATE TABLE tt (i int, j int);
CREATE INDEX ON tt (i, j);

-- Issue #18360 (yb_hash_code compared to constant out of the range [0..65535])
INSERT INTO tt VALUES (1, 2);
-- Negative values
\set explain 'EXPLAIN (COSTS OFF)'
\set Q1 '/*+IndexScan(tt)*/'
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) > -1;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) >= -2;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) = -3;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) < -4;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) <= -5;'
\i :iter_P2

-- Higher than upper bound values
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) > 65536;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) >= 65537;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) = 65538;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) < 65539;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) <= 65540;'
\i :iter_P2

-- Values other than int4
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) > -2147483649;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) >= -2147483650;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) = -2147483651;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) < -2147483652;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) <= -2147483653;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) > 9223372036854775808;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) >= 9223372036854775809;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) = 9223372036854775810;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) < 9223372036854775811;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) <= 9223372036854775812;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) > -0.01;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) >= 123456.78;'
\i :iter_P2
\set query ':P :Q1 SELECT * FROM tt WHERE yb_hash_code(i) = 3.14;'
\i :iter_P2

-- GH18347 : yb_hash_code() in row constructor in predicate with an inequality fails
TRUNCATE TABLE tt;
INSERT INTO tt VALUES(0, 0);
INSERT INTO tt VALUES(0, 1);
INSERT INTO tt VALUES(0, 2);
INSERT INTO tt VALUES(0, 3);
INSERT INTO tt VALUES(1, 0);
INSERT INTO tt VALUES(1, 1);
INSERT INTO tt VALUES(2, 2);
INSERT INTO tt VALUES(3, 3);
INSERT INTO tt VALUES(2147483647, 0);
INSERT INTO tt VALUES(2147483647, 1);
INSERT INTO tt VALUES(2147483647, 2);
INSERT INTO tt VALUES(2147483647, 3);
INSERT INTO tt VALUES(-2147483648, 0);
INSERT INTO tt VALUES(-2147483648, 1);
INSERT INTO tt VALUES(-2147483648, 2);
INSERT INTO tt VALUES(-2147483648, 3);

-- Failing query pattern.
\set Q1 '/*+IndexOnlyScan(tt)*/'
\set Q2 '/*+SeqScan(tt)*/'
\set query ':P :Q SELECT i, j, yb_hash_code(i) hash_code_i, yb_hash_code(1) hash_code_1 FROM tt WHERE row(j, yb_hash_code(i)) > row(1, yb_hash_code(1)) ORDER BY 1, 2;'
\set Pnext :iter_Q2
\i :iter_P2

-- Try variation.
\set query ':P :Q SELECT i, j, yb_hash_code(i) hash_code_i, yb_hash_code(2) hash_code_2 FROM tt WHERE row(j, yb_hash_code(i)) < row(1, yb_hash_code(2)) ORDER BY 1, 2;'
\i :iter_P2

-- Try a 1 element IN with row constructor. Should get an index scan since this turns into an equality.
\set query ':P :Q SELECT i, j, yb_hash_code(i) hash_code_i, yb_hash_code(1) hash_code_1 FROM tt WHERE row(j, yb_hash_code(i)) IN (row(1, yb_hash_code(1))) ORDER BY 1, 2;'
\i :iter_P2

-- Try an IN with yb_hash_code().
-- TODO(#23362): expect index condition to be pushed down rather than PG filter.
\set query ':P :Q SELECT i, j, yb_hash_code(i) hash_code_i, yb_hash_code(1) hash_code_1, yb_hash_code(2) hash_code_2 FROM tt WHERE yb_hash_code(i) IN (yb_hash_code(1), yb_hash_code(2)) ORDER BY 1, 2;'
\i :iter_P2

drop table tt;
