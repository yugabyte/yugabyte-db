-- Test expression pushdown in scans
SET yb_enable_expression_pushdown to on;
-- For environment independent output of timestamps
SET timezone to 'UTC';

CREATE TABLE pushdown_test(k int primary key, i1 int, t1 text, ts1 timestamp, ts2 timestamp with time zone, r1 numrange, a1 int[]);
INSERT INTO pushdown_test VALUES (1, 1, 'value1', '2021-11-11 11:11:11', '2021-11-11 11:11:11+1', '[0, 0)', '{1, NULL, 42}');
INSERT INTO pushdown_test VALUES (2, 202, 'value2', NULL, '2222-02-22 22:22:22-7', numrange(-1, NULL), '{-2, 2, 122, -122}');

-- Simple expression (column = constant)
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE i1 = 1;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE t1 LIKE 'val%';
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE ts1 < '2021-11-11 11:11:12';
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE ts2 < '2021-11-11 11:11:10Z';

SELECT * FROM pushdown_test WHERE i1 = 1;
SELECT * FROM pushdown_test WHERE t1 LIKE 'val%';
SELECT * FROM pushdown_test WHERE ts1 < '2021-11-11 11:11:12';
SELECT * FROM pushdown_test WHERE ts2 < '2021-11-11 11:11:10Z';

-- Simple function on one column
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE isfinite(ts1);

SELECT * FROM pushdown_test WHERE isfinite(ts1);

-- Simple function on multiple columns
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE left(t1, i1) = 'v';

SELECT * FROM pushdown_test WHERE left(t1, i1) = 'v';

-- Functions safe for pushdown (yb_safe_funcs_for_pushdown.c)
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE i1 < 10 + random() * 90;

SELECT * FROM pushdown_test WHERE i1 < 10 + random() * 90;

-- Null test
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE ts1 IS NULL;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE ts1 IS NOT NULL;

SELECT * FROM pushdown_test WHERE ts1 IS NULL;
SELECT * FROM pushdown_test WHERE ts1 IS NOT NULL;

-- Boolean expression
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE i1 = 1 OR NOT isfinite(ts1) AND ts2 > '2001-01-01 01:01:01-7'::timestamptz;

SELECT * FROM pushdown_test WHERE i1 = 1 OR NOT isfinite(ts1) AND ts2 > '2001-01-01 01:01:01-7'::timestamptz;

-- Case expression
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE CASE WHEN i1 % 2 = 0 THEN ts1 < '2021-11-12' WHEN i1 % 2 = 1 THEN ts2 > '2022-01-01 00:00:00-7' END;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE CASE i1 % 2 WHEN 0 THEN ts1 < '2021-11-12' WHEN 1 THEN ts2 > '2022-01-01 00:00:00-7' END;

SELECT * FROM pushdown_test WHERE CASE WHEN i1 % 2 = 0 THEN ts1 < '2021-11-12' WHEN i1 % 2 = 1 THEN ts2 > '2022-01-01 00:00:00-7' END;

-- Aggregates
EXPLAIN (COSTS FALSE) SELECT count(*) FROM pushdown_test;
EXPLAIN (COSTS FALSE) SELECT count(*) FROM pushdown_test WHERE i1 = 1;

SELECT count(*) FROM pushdown_test;
SELECT count(*) FROM pushdown_test WHERE i1 = 1;

-- Parameter
PREPARE s AS SELECT * FROM pushdown_test WHERE i1 = $1;

EXPLAIN (COSTS FALSE) EXECUTE s(1);

EXECUTE s(1);

DEALLOCATE s;

-- Join
CREATE TABLE pushdown_lookup(k int primary key, tag text);
INSERT INTO pushdown_lookup VALUES (1, 'foo'), (2, 'bar'), (3, 'baz');

EXPLAIN (COSTS FALSE) SELECT t.t1 FROM pushdown_test t, pushdown_lookup l WHERE t.k = l.k AND l.tag = 'foo';

SELECT t.t1 FROM pushdown_test t, pushdown_lookup l WHERE t.k = l.k AND l.tag = 'foo';

-- Negative test cases (expressions should not be pushed down)
-- Not immutable functions
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE t1 = concat('value', i1::text);

SELECT * FROM pushdown_test WHERE t1 = concat('value', i1::text);

-- Index scan
CREATE INDEX pushdown_test_i1 ON pushdown_test(i1);
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE i1 = 1;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE i1 = 1 AND t1 = 'value1';

SELECT * FROM pushdown_test WHERE i1 = 1;
SELECT * FROM pushdown_test WHERE i1 = 1 AND t1 = 'value1';

-- Records, ranges, arrays
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE isempty(r1);
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE a1[2] = 2;

SELECT * FROM pushdown_test WHERE isempty(r1);
SELECT * FROM pushdown_test WHERE a1[2] = 2;

-- Pseudo types
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_test WHERE num_nulls(variadic a1) > 0;

SELECT * FROM pushdown_test WHERE num_nulls(variadic a1) > 0;

-- Composite datatype
CREATE TYPE pair AS (first int, second int);
CREATE TABLE pushdown_composite(k int primary key, v pair);
INSERT INTO pushdown_composite VALUES (1, (2, 3));

EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_composite WHERE (v).first = 2;

SELECT * FROM pushdown_composite WHERE (v).first = 2;

-- Enum datatype
CREATE TYPE color AS ENUM('red', 'green', 'blue');
CREATE TABLE pushdown_enum(k int, c color, x int);
INSERT INTO pushdown_enum VALUES (1, 'red', 255);

EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_enum WHERE c = 'red';
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_enum WHERE c::text = 'red';

SELECT * FROM pushdown_enum WHERE c = 'red';
SELECT * FROM pushdown_enum WHERE c::text = 'red';

-- Collation
CREATE TABLE pushdown_collation(k int primary key, v text COLLATE "ucs_basic");
INSERT INTO pushdown_collation VALUES (1, 'foo');

-- Do not pushdown operation on column with collation other than C
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_collation WHERE v = 'foo';
SELECT * FROM pushdown_collation WHERE v = 'foo';

CREATE TABLE pushdown_index(k1 int, k2 int, v1 int, v2 int, v3 int, v4 int, v5 text, primary key (k1, k2));
CREATE INDEX pushdown_index_v1_v2_v3v4_idx ON pushdown_index(v1, v2, (v3 + v4) ASC);
INSERT INTO pushdown_index VALUES (1, 1, 1, 1, 1, 0, 'row 1');
INSERT INTO pushdown_index VALUES (1, 2, 3, 4, 5, 6, 'row 2');
INSERT INTO pushdown_index VALUES (1, 20, 20, 20, 20, 20, 'row 3');
INSERT INTO pushdown_index VALUES (2, 1, 1, 2, 3, 4, 'row 4');
INSERT INTO pushdown_index VALUES (2, 2, 1, 3, 2, 1, 'row 5');
INSERT INTO pushdown_index VALUES (2, 20, 1, 20, 40, 50, 'row 6');
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = 2;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE k1 = 1 AND k2/10 = 0;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = v1;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = v1 AND CASE v2 % 2 WHEN 0 THEN v3 < 0 WHEN 1 THEN v3 > 0 END;
EXPLAIN (COSTS FALSE) SELECT v2 FROM pushdown_index WHERE v1 = 1;
EXPLAIN (COSTS FALSE) SELECT v2 FROM pushdown_index WHERE v1 = 1 AND v2 > v1;
EXPLAIN (COSTS FALSE) SELECT v2, v3, v4 FROM pushdown_index WHERE v1 = 1 AND v2 > v1;
EXPLAIN (COSTS FALSE) SELECT v2 FROM pushdown_index WHERE v1 = 1 AND v2 = v3 + v4;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE v1 = 1 AND v2 > v1 AND v3 > v2;
EXPLAIN (COSTS FALSE) SELECT * FROM pushdown_index WHERE v1 = 1 AND v2 > v1 AND v3 > v2 AND CASE v3 % 2 WHEN 0 THEN v4 < 0 WHEN 1 THEN v4 > 0 END;
SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = 2;
SELECT * FROM pushdown_index WHERE k1 = 1 AND k2/10 = 0;
SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = v1;
SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = v1 AND CASE v2 % 2 WHEN 0 THEN v3 < 0 WHEN 1 THEN v3 > 0 END;
SELECT v2 FROM pushdown_index WHERE v1 = 1;
SELECT v2 FROM pushdown_index WHERE v1 = 1 AND v2 > v1;
SELECT v2, v3, v4 FROM pushdown_index WHERE v1 = 1 AND v2 > v1;
SELECT v2 FROM pushdown_index WHERE v1 = 1 AND v2 = v3 + v4;
SELECT * FROM pushdown_index WHERE v1 = 1 AND v2 > v1 AND v3 > v2;
SELECT * FROM pushdown_index WHERE v1 = 1 AND v2 > v1 AND v3 > v2 AND CASE v3 % 2 WHEN 0 THEN v4 < 0 WHEN 1 THEN v4 > 0 END;

PREPARE pk_param AS SELECT * FROM pushdown_index WHERE k1 = 1 AND k2 = v1 + $1;
EXPLAIN (COSTS FALSE) EXECUTE pk_param(0);
EXECUTE pk_param(0);
DEALLOCATE pk_param;

PREPARE si_param AS SELECT * FROM pushdown_index WHERE v1 = 1 AND v2 > v1 + $1 AND v3 > v2 + $2;
EXPLAIN (COSTS FALSE) EXECUTE si_param(0, 0);
EXECUTE si_param(0, 0);
DEALLOCATE si_param;

-- Index scan with remote filter on a system table
EXPLAIN (COSTS FALSE) SELECT relname, relkind FROM pg_class WHERE relname LIKE 'pushdown_c%';
SELECT relname, relkind FROM pg_class WHERE relname LIKE 'pushdown_c%';

DROP TABLE pushdown_index;
DROP TABLE pushdown_test;
DROP TABLE pushdown_lookup;
DROP TABLE pushdown_composite;
DROP TABLE pushdown_enum;
DROP TABLE pushdown_collation;
DROP TYPE pair;
DROP TYPE color;
