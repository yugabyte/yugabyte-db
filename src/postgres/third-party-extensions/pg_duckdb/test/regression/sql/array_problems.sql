set duckdb.force_execution TO FALSE;
CREATE TABLE s (a text[]);
INSERT INTO s VALUES (ARRAY['abc', 'def', 'ghi']);
-- Because the next table is created using a CTAS, attndims is set to 0. That
-- confused us in the past. See #556 for details. We assume it's single
-- dimensional now.
CREATE TABLE t AS TABLE s;
SELECT * FROM s;
SELECT * FROM t;
SET duckdb.force_execution TO true;
SELECT * FROM s;
SELECT * FROM t;

-- Processing arryas of different dimensions in the same column is not something
-- that DuckDB can handle.
INSERT INTO s VALUES(ARRAY[['a', 'b'],['c','d']]);
SELECT * FROM s;
TRUNCATE s;

-- And we assume that the table metadata is correct about the dimensionality.
-- So even if the stored dimensionality is consistently wrong we will throw an
-- error.
INSERT INTO s VALUES(ARRAY[['a', 'b'],['c','d']]);
SELECT * FROM s;
-- But if you change the definition of the table, we will be able to handle it.
ALTER TABLE s ALTER COLUMN a SET DATA TYPE text[][];
SELECT * FROM s;

-- Similarly Posgres cannot support nested lists where different sub-lists at
-- the same level have a different length.
SELECT * FROM duckdb.query($$ SELECT ARRAY[ARRAY[1,2], ARRAY[3,4,5]] arr $$);

DROP TABLE s, t;
