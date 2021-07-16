-- Tests for nlssort
-- YB: we do not support SQL_ASCII encoding
\set ECHO none
SET client_min_messages = error;
DROP DATABASE IF EXISTS regression_sort;
CREATE DATABASE regression_sort WITH TEMPLATE = template0 ENCODING='UTF8' LC_COLLATE='C' LC_CTYPE='en_US.UTF-8';
\c regression_sort
SET client_min_messages = error;
CREATE EXTENSION orafce;
SET client_min_messages = default;
CREATE TABLE test_sort (name TEXT);
INSERT INTO test_sort VALUES ('red'), ('brown'), ('yellow'), ('Purple');
SELECT * FROM test_sort ORDER BY NLSSORT(name, 'C');
SELECT * FROM test_sort ORDER BY NLSSORT(name, '');
SELECT set_nls_sort('invalid');
SELECT * FROM test_sort ORDER BY NLSSORT(name);
SELECT set_nls_sort('');
SELECT * FROM test_sort ORDER BY NLSSORT(name);