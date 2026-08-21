SET duckdb.log_pg_explain = true;
CREATE TABLE query_filter_int(a INT);
INSERT INTO query_filter_int SELECT g FROM generate_series(1,100) g;
SELECT COUNT(*) FROM query_filter_int WHERE a  <= 50;
DROP TABLE query_filter_int;

CREATE TABLE query_filter_float(a FLOAT8);
INSERT INTO query_filter_float VALUES (0.9), (1.0), (1.1);
SELECT COUNT(*) FROM query_filter_float WHERE a < 1.0;
SELECT COUNT(*) FROM query_filter_float WHERE a <= 1.0;
SELECT COUNT(*) FROM query_filter_float WHERE a < 1.1;
DROP TABLE query_filter_float;

CREATE TABLE query_filter_varchar(a VARCHAR, b VARCHAR);
INSERT INTO query_filter_varchar VALUES ('t1', 't%'), ('t2', '%%'), ('t1', '');
SELECT COUNT(*)FROM query_filter_varchar WHERE a = 't1';
SELECT COUNT(a) FROM query_filter_varchar WHERE a = 't1';
SELECT a, COUNT(*) FROM query_filter_varchar WHERE a = 't1' GROUP BY a;

INSERT INTO query_filter_varchar VALUES ('at1'), ('btt'), ('%t%t%t%'), ('_t_t_t_');
-- Pushed down to PG executor
SELECT a FROM query_filter_varchar WHERE a LIKE '%t%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%t%';
SELECT a FROM query_filter_varchar WHERE a LIKE 't%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE 't%';
SELECT a FROM query_filter_varchar WHERE a LIKE '%t';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%t';
SELECT a FROM query_filter_varchar WHERE a LIKE '_t_';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '_t_';
SELECT a FROM query_filter_varchar WHERE a LIKE 't_';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE 't_';
SELECT a FROM query_filter_varchar WHERE a LIKE '_t';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '_t';
SELECT a FROM query_filter_varchar WHERE a LIKE a;
SELECT a FROM query_filter_varchar WHERE a NOT LIKE a;
SELECT a FROM query_filter_varchar WHERE 'txxx' LIKE b;
SELECT a FROM query_filter_varchar WHERE 'txxx' NOT LIKE b;
SELECT a FROM query_filter_varchar WHERE upper(a) = 'BTT';
SELECT a FROM query_filter_varchar WHERE upper(a) != 'BTT';
SELECT a FROM query_filter_varchar WHERE upper(a) < 'B';
SELECT a FROM query_filter_varchar WHERE upper(a) > 'B';
SELECT a FROM query_filter_varchar WHERE upper(a) <= 'B';
SELECT a FROM query_filter_varchar WHERE upper(a) >= 'B';
SELECT a FROM query_filter_varchar WHERE upper(a) BETWEEN 'BAA' AND 'BXX';
-- Interestingly the three below get converted to a COMPARE_BETWEEN operation
-- by duckdb, eventhough ther's no valid way of representing exclusive ranges
-- in SQL.
SELECT a FROM query_filter_varchar WHERE upper(a) >= 'BAA' AND upper(a) <= 'BXX';
SELECT a FROM query_filter_varchar WHERE upper(a) >= 'BAA' AND upper(a) < 'BXX';
SELECT a FROM query_filter_varchar WHERE upper(a) > 'BAA' AND upper(a) < 'BXX';
SELECT a FROM query_filter_varchar WHERE upper(a) NOT BETWEEN 'BAA' AND 'BXX';
SELECT a FROM query_filter_varchar WHERE upper(a) NOT BETWEEN upper('baa') AND upper('bxx');
SELECT a FROM query_filter_varchar WHERE lower(a) > 'b';
SELECT a FROM query_filter_varchar WHERE upper(a) IS DISTINCT FROM 'BTT';
SELECT a FROM query_filter_varchar WHERE upper(a) IS NOT DISTINCT FROM 'BTT';
SELECT a FROM query_filter_varchar WHERE upper(a) = 'BTT' OR upper(a) = '_T_T_T_' OR (upper(a) >= 'T2' AND upper(a) < 'T8' AND upper(a) != 'T5');
SELECT a FROM query_filter_varchar WHERE upper(a) LIKE '%T%';
SELECT a FROM query_filter_varchar WHERE upper(a) LIKE '%T%';

-- test escaping

-- PG uses \ as default escape character, but DuckDB has
-- no default escape character, so this might fail in some cases.
SELECT a FROM query_filter_varchar WHERE a LIKE '%\%t\%%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%\%t\%%';
SELECT a FROM query_filter_varchar WHERE a LIKE 't\%%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE 't\%\%';
SELECT a FROM query_filter_varchar WHERE a LIKE '%\%t';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%\%t';
SELECT a FROM query_filter_varchar WHERE a LIKE '%\_t\_%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%\_t\_%';
SELECT a FROM query_filter_varchar WHERE a LIKE 't\_%';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE 't\_%';
SELECT a FROM query_filter_varchar WHERE a LIKE '%\_t';
SELECT a FROM query_filter_varchar WHERE a NOT LIKE '%\_t';
SELECT * FROM duckdb.query($$ SELECT a FROM pgduckdb.public.query_filter_varchar WHERE contains(a, '_') $$);
SELECT * FROM duckdb.query($$ SELECT a FROM pgduckdb.public.query_filter_varchar WHERE contains(a, '%') $$);
SELECT * FROM duckdb.query($$ SELECT a FROM pgduckdb.public.query_filter_varchar WHERE contains(a, '\') $$);

-- Not pushed down because they are constant filters. Should not crash.
SELECT a FROM query_filter_varchar WHERE a LIKE NULL;
SELECT a FROM query_filter_varchar WHERE NULL LIKE a;
SELECT a FROM query_filter_varchar WHERE a LIKE NULL;
SELECT a FROM query_filter_varchar WHERE NULL LIKE b;
-- Not pushed down because filter involves multiple columns. Should not crash.
SELECT a FROM query_filter_varchar WHERE a LIKE b;
-- Not pushed because DuckDB transforms this into a hash join.
SELECT a FROM query_filter_varchar WHERE upper(a) IN ('BTT', 'CTT');
SELECT a FROM query_filter_varchar WHERE upper(a) NOT IN ('BTT', 'CTT');

DROP TABLE query_filter_varchar;

CREATE TABLE query_filter_output_column(a INT, b VARCHAR, c FLOAT8);
INSERT INTO query_filter_output_column VALUES (1, 't1', 1.0), (2, 't1', 2.0), (2, 't2', 1.0);
-- Projection ids list will be used (column `a`is not needed after scan)
SELECT b FROM query_filter_output_column WHERE a = 2;
-- Column ids list used because both of fetched column are used after scan
SELECT a, b FROM query_filter_output_column WHERE b = 't1';
-- Column ids list used because both of fetched column are used after scan.
-- Swapped order of table columns.
SELECT b, a FROM query_filter_output_column WHERE b = 't1';
-- Projection ids list will be used (column `b`is not needed after scan)
SELECT a, c FROM query_filter_output_column WHERE b = 't1';
-- All columns in tuple unordered
SELECT c, a, b FROM query_filter_output_column WHERE a = 2;
DROP TABLE query_filter_output_column;
