-- Verify that SELECTs of a single query are on a single read point.
CREATE table test1(id int primary key, value int);
CREATE table test2(id int primary key, value int);
INSERT INTO test1 VALUES(1, 1);
INSERT INTO test2 VALUES(2, 2);
EXPLAIN INSERT INTO test2 SELECT id + 2, value FROM test1 UNION ALL SELECT id + 2, value FROM test2;
INSERT INTO test2 SELECT id + 2, value FROM test1 UNION ALL SELECT id + 2, value FROM test2;
SELECT * FROM test2 ORDER BY id;

-- Verify above behavior for Index Scans.
CREATE INDEX ON test1(value);
CREATE INDEX ON test2(value);
EXPLAIN INSERT INTO test2 SELECT id + 4, value + 1 FROM test1 WHERE value=1 UNION ALL SELECT id + 4, value FROM test2 WHERE value=2;
INSERT INTO test2 SELECT id + 4, value + 1 FROM test1 WHERE value=1 UNION ALL SELECT id + 4, value FROM test2 WHERE value=2;
SELECT * FROM test2 ORDER BY id;

-- Verify above behavior for Index Only Scans.
EXPLAIN INSERT INTO test2 SELECT 9, value FROM test1 WHERE value=1 UNION ALL SELECT 10, value FROM test2 WHERE value=1;
INSERT INTO test2 SELECT 9, value FROM test1 WHERE value=1 UNION ALL SELECT 10, value FROM test2 WHERE value=1;
SELECT * FROM test2 ORDER BY id;

-- Verify above behavior for Primary-key index scans.
EXPLAIN INSERT INTO test2 SELECT id + 10, value FROM test1 WHERE id=1 UNION ALL SELECT id+2, value FROM test2 WHERE id=11;
INSERT INTO test2 SELECT id + 10, value FROM test1 WHERE id=1 UNION ALL SELECT id+2, value FROM test2 WHERE id=11;
SELECT * FROM test2 ORDER BY id;

-- Verify above behavior within transaction block.
BEGIN;
INSERT INTO test2 SELECT id + 11, value FROM test1 WHERE id=1 UNION ALL SELECT id+2, value FROM test2 WHERE id=12;
SELECT * FROM test2 ORDER BY id;
COMMIT;

-- Verify above behavior within SQL functions.
CREATE FUNCTION testfunc() RETURNS SETOF test2 AS '
INSERT INTO test2 SELECT id + 12, value FROM test1 WHERE id=1 UNION ALL SELECT id+2, value FROM test2 WHERE id=13;
SELECT * FROM test2 ORDER BY id;
' LANGUAGE SQL;

SELECT * FROM testfunc();

-- Verify above behavior within Procedures.
CREATE PROCEDURE testproc()
LANGUAGE SQL
AS $$
INSERT INTO test2 SELECT id + 13, value FROM test1 WHERE id=1 UNION ALL SELECT id+2, value FROM test2 WHERE id=14;
INSERT INTO test2 SELECT id + 1, value FROM test2 WHERE id = 14;
$$;

CALL testproc();
SELECT * FROM test2 ORDER BY id;
