-- YB AGGREGATES TEST (for pushdown)

--
-- Test basic aggregates and verify overflow is handled properly.
--
CREATE TABLE ybaggtest (
    id         int PRIMARY KEY,
    int_2      int2,
    int_4      int4,
    int_8      int8,
    float_4    float4,
    float_8    float8
);

-- Insert maximum integer values multiple times to force overflow on SUM (both in DocDB and PG).
INSERT INTO ybaggtest VALUES (1, 32767, 2147483647, 9223372036854775807, 1.1, 2.2);
INSERT INTO ybaggtest
    SELECT series, t.int_2, t.int_4, t.int_8, t.float_4, t.float_8
    FROM ybaggtest as t CROSS JOIN generate_series(2, 100) as series;

-- Verify COUNT(*) returns proper value.
SELECT COUNT(*) FROM ybaggtest;

-- Delete row, verify COUNT(*) returns proper value.
DELETE FROM ybaggtest WHERE id = 100;
SELECT COUNT(*) FROM ybaggtest;

-- Verify selecting different aggs for same column works.
SELECT SUM(int_4), MAX(int_4), MIN(int_4), SUM(int_2), MAX(int_2), MIN(int_2) FROM ybaggtest;

-- Verify SUMs are correct for all fields and do not overflow.
SELECT SUM(int_2), SUM(int_4), SUM(int_8), SUM(float_4), SUM(float_8) FROM ybaggtest;

-- Verify shared aggregates work as expected.
SELECT SUM(int_4), SUM(int_4) + 1 FROM ybaggtest;

-- Verify NaN float values are respected by aggregates.
INSERT INTO ybaggtest (id, float_4, float_8) VALUES (101, 'NaN', 'NaN');
SELECT COUNT(float_4), SUM(float_4), MAX(float_4), MIN(float_4) FROM ybaggtest;
SELECT COUNT(float_8), SUM(float_8), MAX(float_8), MIN(float_8) FROM ybaggtest;

--
-- Test NULL rows are handled properly by COUNT.
--
-- Create table without primary key.
CREATE TABLE ybaggtest2 (
    a int
);

-- Insert NULL rows.
INSERT INTO ybaggtest2 VALUES (NULL), (NULL), (NULL);

-- Insert regular rows.
INSERT INTO ybaggtest2 VALUES (1), (2), (3);

-- Verify NULL rows are included in COUNT(*) but not in COUNT(row).
SELECT COUNT(*) FROM ybaggtest2;
SELECT COUNT(a) FROM ybaggtest2;
SELECT COUNT(*), COUNT(a) FROM ybaggtest2;

-- Verify MAX/MIN respect NULL values.
SELECT MAX(a), MIN(a) FROM ybaggtest2;

CREATE TABLE digit(k INT PRIMARY KEY, v TEXT NOT NULL);
INSERT INTO digit VALUES(1, 'one'), (2, 'two'), (3, 'three'), (4, 'four'), (5, 'five'), (6, 'six');
CREATE TABLE test(k INT PRIMARY KEY);
ALTER TABLE test ADD v1 int DEFAULT 5;
ALTER TABLE test ADD v2 int DEFAULT 10;
INSERT INTO test VALUES(1), (2), (3);
SELECT COUNT(*) FROM test;
SELECT COUNT(k) FROM test;
SELECT COUNT(v1) FROM test;
SELECT COUNT(v2) FROM test;
SELECT * FROM digit AS d INNER JOIN (SELECT COUNT(v2) AS count FROM test) AS c ON (d.k = c.count);
INSERT INTO test VALUES(4, NULL, 10), (5, 5, NULL), (6, 5, NULL);
SELECT COUNT(*) FROM test;
SELECT COUNT(k) FROM test;
SELECT COUNT(v1) FROM test;
SELECT COUNT(v2) FROM test;
SELECT * FROM digit AS d INNER JOIN (SELECT COUNT(*) AS count FROM test) AS c ON (d.k = c.count);
SELECT * FROM digit AS d INNER JOIN (SELECT COUNT(k) AS count FROM test) AS c ON (d.k = c.count);
SELECT * FROM digit AS d INNER JOIN (SELECT COUNT(v1) AS count FROM test) AS c ON (d.k = c.count);
SELECT * FROM digit AS d INNER JOIN (SELECT COUNT(v2) AS count FROM test) AS c ON (d.k = c.count);

DROP TABLE test;
DROP TABLE digit;

CREATE TABLE test(K INT PRIMARY KEY, v1 INT NOT NULL, v2 INT NOT NULL);
INSERT INTO test VALUES(1, 1, 1), (2, 2, 2), (3, 3, 3);
AlTER TABLE test DROP v1;
SELECT MIN(v2) FROM test;
SELECT MAX(v2) FROM test;
SELECT SUM(v2) FROM test;
SELECT COUNT(v2) FROM test;
