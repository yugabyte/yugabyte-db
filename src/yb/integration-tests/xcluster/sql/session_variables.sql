--
-- Test proper handling of session variables.
--
-- tablespaces are already set up at this point.

CREATE TABLE normal_table(id INT PRIMARY KEY);


-- search_path can put tables in a different schema.
RESET ALL;
CREATE SCHEMA different_schema;
SET search_path = different_schema;
CREATE TABLE table_with_different_schema(id INT PRIMARY KEY);


-- yb_use_hash_splitting_by_default affects whether we get hash or range sharding.
RESET ALL;
SET yb_use_hash_splitting_by_default = false;
CREATE TABLE table_with_range_sharding(a INT PRIMARY KEY);


-- default_tablespace affects what tablespace a table gets put in.
RESET ALL;
SET default_tablespace = "user's tablespace"; -- fix syntax highlighting: '
CREATE TABLE table_in_different_tablespace(id INT PRIMARY KEY);


-- Test two session variables at once.
RESET ALL;
SET yb_use_hash_splitting_by_default = false;
SET default_tablespace = 'user''s tablespace';
CREATE TABLE table_in_different_tablespace_with_range_sharding(id INT PRIMARY KEY);


-- Test session variables that control string literal parsing.
RESET ALL;
SET standard_conforming_strings = off;
CREATE TABLE nonconforming_strings_table (
  x text DEFAULT 'a\nb'
);
SET backslash_quote = on;
SET escape_string_warning = on;
CREATE TABLE table_with_backslash_quoted (
  x text DEFAULT 'it\'s ok' -- fix syntax highlighting: '
);


-- Test session variables that affect date parsing.
RESET ALL;
SET DateStyle = 'SQL, DMY';
CREATE TABLE table_with_default_date (
  d date DEFAULT '1/8/1999'
);


-- Test session variables that affect time zone parsing.
RESET ALL;
SET TimeZone = 'UTC';
CREATE TABLE table_with_UTC_timezone (
  ts timestamptz DEFAULT '2004-10-19 10:23:54'
);
SET TimeZone = 'Asia/Tokyo';
CREATE TABLE table_with_Tokyo_timezone (
  ts timestamptz DEFAULT '2004-10-19 10:23:54'
);


-- Test session variables that affect parsing of intervals.
RESET ALL;
SET IntervalStyle = 'sql_standard';
CREATE TABLE table_with_default_interva (
  iv interval DEFAULT '-1 2:03:04'
);


-- Test session variables that affect parsing of function bodies.
RESET ALL;
SET check_function_bodies = false;
CREATE FUNCTION check_test() RETURNS int AS $$
    SELECT 1 FROM fake_table;
$$ LANGUAGE sql


-- Test session variables that affect client encoding of input.
RESET ALL;
SET client_encoding = 'LATIN1';
CREATE TABLE table_created_using_latin_encoding (
  "☃" text
);
SET client_encoding = 'UTF8';
CREATE TABLE table_created_using_utf8_encoding (
  "☃" text
);
