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

-- For PG >= 15 explicitly set the locale provider libc when creating the
-- database with SQL_ASCII encoding. Otherwise during installcheck the new
-- database may use the ICU locale provider (from template0) which does not
-- support this encoding.

SELECT current_setting('server_version_num')::integer >= 150000
       AS set_libc_locale_provider \gset

\if :set_libc_locale_provider
CREATE DATABASE regression_sort WITH TEMPLATE = template0 ENCODING='SQL_ASCII' LC_COLLATE='C' LC_CTYPE='C' LOCALE_PROVIDER='libc';
\else
CREATE DATABASE regression_sort WITH TEMPLATE = template0 ENCODING='SQL_ASCII' LC_COLLATE='C' LC_CTYPE='C';
\endif

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
