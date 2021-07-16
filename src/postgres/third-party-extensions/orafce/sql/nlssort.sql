-- Tests for nlssort
\set ECHO none
SET client_min_messages = error;
DROP DATABASE IF EXISTS regression_sort;
CREATE DATABASE regression_sort WITH TEMPLATE = template0 ENCODING='SQL_ASCII' LC_COLLATE='C' LC_CTYPE='C';
\c regression_sort
SET client_min_messages = error;
CREATE EXTENSION orafce;
SET client_min_messages = default;
CREATE TABLE test_sort (name TEXT);
INSERT INTO test_sort VALUES ('red'), ('brown'), ('yellow'), ('Purple');
SELECT * FROM test_sort ORDER BY NLSSORT(name, 'en_US.utf8');
SELECT * FROM test_sort ORDER BY NLSSORT(name, '');
SELECT set_nls_sort('invalid');
SELECT * FROM test_sort ORDER BY NLSSORT(name);
SELECT set_nls_sort('');
SELECT * FROM test_sort ORDER BY NLSSORT(name);
SELECT set_nls_sort('en_US.utf8');
SELECT * FROM test_sort ORDER BY NLSSORT(name);
INSERT INTO test_sort VALUES(NULL);
SELECT * FROM test_sort ORDER BY NLSSORT(name);
