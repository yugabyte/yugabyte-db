-- Tests for nlssort

-- Skip this test unless it's a Linux/glibc system with the "en_US.utf8" locale installed.
SELECT getdatabaseencoding() <> 'UTF8' OR
       NOT EXISTS (SELECT 1 FROM pg_collation WHERE collname = 'en_US' AND collencoding = pg_char_to_encoding('UTF8')) OR
       version() !~ 'linux-gnu'
       AS skip_test \gset
\if :skip_test
\quit
\endif

\set ECHO none
SET client_min_messages = error;
DROP DATABASE IF EXISTS regression_sort;

-- YB: we do not support SQL_ASCII encoding
CREATE DATABASE regression_sort WITH TEMPLATE = template0 ENCODING='UTF8' LC_COLLATE='C' LC_CTYPE='en_US.UTF-8';

\c regression_sort
SET client_min_messages = error;

CREATE EXTENSION orafce;

SET search_path TO public, oracle;

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
