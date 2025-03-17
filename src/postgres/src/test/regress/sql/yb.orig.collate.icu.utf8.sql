CREATE SCHEMA collate_tests;
SET search_path = collate_tests;
SET yb_test_collation = false;


CREATE TABLE collate_test1 (
    a int,
    b text COLLATE "en-x-icu" NOT NULL
);

\d collate_test1

CREATE TABLE collate_test_fail (
    a int,
    b text COLLATE "ja_JP.eucjp-x-icu"
);

CREATE TABLE collate_test_fail (
    a int,
    b text COLLATE "foo-x-icu"
);

CREATE TABLE collate_test_fail (
    a int COLLATE "en-x-icu",
    b text
);

CREATE TABLE collate_test_like (
    LIKE collate_test1
);

\d collate_test_like

CREATE TABLE collate_test2 (
    a int,
    b text COLLATE "sv-x-icu"
);

CREATE TABLE collate_test3 (
    a int,
    b text COLLATE "C"
);

INSERT INTO collate_test1 VALUES (1, 'abc'), (2, 'äbc'), (3, 'bbc'), (4, 'ABC');
INSERT INTO collate_test2 SELECT * FROM collate_test1;
INSERT INTO collate_test3 SELECT * FROM collate_test1;

-- Repeat the test with expression pushdown disabled
set yb_enable_expression_pushdown to off;
EXPLAIN (costs off) SELECT * FROM collate_test1 WHERE b >= 'bbc' order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test2 WHERE b >= 'bbc' order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test3 WHERE b >= 'bbc' order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test3 WHERE b >= 'BBC' order by 1;

SELECT * FROM collate_test1 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test2 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test3 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test3 WHERE b >= 'BBC' order by 1;

EXPLAIN (costs off) SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test1 WHERE b >= 'bbc' COLLATE "C" order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "C" order by 1;
EXPLAIN (costs off) SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "en-x-icu" order by 1;

SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' order by 1;
SELECT * FROM collate_test1 WHERE b >= 'bbc' COLLATE "C" order by 1;
SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "C" order by 1;
SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "en-x-icu" order by 1;
set yb_enable_expression_pushdown to on;


CREATE TABLE tab1(id varchar(10));
INSERT INTO tab1 values ('aaaa');
\d tab1

-- test rewrites
CREATE OR REPLACE FUNCTION trig_rewrite() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'rewriting table';
END;
$$;
create event trigger event_rewrite on table_rewrite
  execute procedure trig_rewrite();

ALTER TABLE tab1 ALTER COLUMN id SET DATA TYPE varchar(5) COLLATE "en-US-x-icu"; -- rewrite
\d tab1

CREATE TABLE tab2(id varchar(10) COLLATE "en-US-x-icu");
CREATE INDEX tab2_id_idx on tab2(id COLLATE "C" desc);
INSERT INTO tab2 VALUES ('aaaa');
\d tab2
ALTER TABLE tab2 alter COLUMN id SET DATA TYPE varchar(20) COLLATE "en-US-x-icu"; -- no rewrite
\d tab2

SELECT CASE WHEN v LIKE '%linux%' THEN 'true' ELSE 'false' END as linux FROM (SELECT version() as v) as v \gset
\if :linux
  \set en_us_collname "en_US.utf8"
  \set zh_cn_collname "zh_CN.utf8"
  \set fr_fr_collname "fr_FR.utf8"
  \set utf8_result 'aaa ZZZ'
  \set posix_result 'ZZZ aaa'
\else
  \set en_us_collname "en_US.UTF-8"
  \set zh_cn_collname "zh_CN.UTF-8"
  \set fr_fr_collname "fr_FR.UTF-8"
  \set utf8_result 'ZZZ aaa'
  \set posix_result 'ZZZ aaa'
\endif

\set default_result 'ZZZ aaa'
\set ucs_basic_result 'ZZZ aaa'
\set en_us_x_icu_result 'aaa ZZZ'

-- test YB default db
\c yugabyte
CREATE DATABASE test_default_db
TEMPLATE template0;
\c test_default_db
CREATE TABLE tab(id text);
INSERT INTO tab VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab ORDER BY id) as id \gset
SELECT :'id' = :'default_result';


-- test YB "POSIX" db
\c yugabyte
CREATE DATABASE test_posix_db
LOCALE "POSIX"
TEMPLATE template0;
\c test_posix_db
CREATE TABLE tab(id text);
INSERT INTO tab VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab ORDER BY id) as id \gset
SELECT :'id' = :'posix_result';

-- test YB "ucs_basic" db
\c yugabyte
CREATE DATABASE test_ucs_basic_db
LOCALE "ucs_basic"
TEMPLATE template0;

-- test YB "ucs_basic" table
\c yugabyte
CREATE TABLE tab2(id text COLLATE "ucs_basic");
INSERT INTO tab2 VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab2 ORDER BY id) as id \gset
SELECT :'id' = :'ucs_basic_result';

-- test YB en_US.UTF-8 db
\c yugabyte
CREATE DATABASE test_en_us_utf8_db
LOCALE "en_US.UTF-8"
TEMPLATE template0;
\c test_en_us_utf8_db
CREATE TABLE tab(id text);
INSERT INTO tab VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab ORDER BY id) as id \gset
SELECT :'id' = :'utf8_result';

-- test YB en-US-x-icu
\c yugabyte
CREATE DATABASE test_en_us_x_icu_db
LOCALE_PROVIDER icu
ICU_LOCALE "en-US-x-icu"
LOCALE "en_US.UTF-8"
TEMPLATE template0;
\c test_en_us_x_icu_db
CREATE TABLE tab(id text);
INSERT INTO tab VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab ORDER BY id) as id \gset
SELECT :'id' = :'en_us_x_icu_result';

-- test YB en_US.utf8 db, LOCALE overridden by LC_COLLATE and LC_CTYPE
\c yugabyte
CREATE DATABASE test_en_us_utf8_db2
LOCALE "blar"
LC_COLLATE :en_us_collname
LC_CTYPE :en_us_collname
TEMPLATE template0;
\c test_en_us_utf8_db2
CREATE TABLE tab(id text);
INSERT INTO tab VALUES ('aaa'), ('ZZZ');
SELECT string_agg(id, ' ') as id FROM (SELECT id from tab ORDER BY id) as id \gset
SELECT :'id' = :'utf8_result';

-- test YB blar db, invalid locale name
\c yugabyte
CREATE DATABASE test_blar_db
LC_COLLATE "blar"
LC_CTYPE "blar"
TEMPLATE template0;

-- test YB blar db, invalid locale name
\c yugabyte
CREATE DATABASE test_blar_db
LOCALE "blar"
TEMPLATE template0;

-- test YB zh_CN.utf8 db, unsupported locale name
\c yugabyte
SET yb_test_collation = TRUE;
CREATE DATABASE test_zh_cn_utf8_db
LC_COLLATE :zh_cn_collname
LC_CTYPE :zh_cn_collname
TEMPLATE template0;

-- test YB zh_CN.utf8 db, unsupported locale name
\c yugabyte
SET yb_test_collation = TRUE;
CREATE DATABASE test_zh_cn_utf8_db
LOCALE :zh_cn_collname
TEMPLATE template0;

-- test YB blar collation, unsupported locale name
CREATE COLLATION blar1 (LOCALE = 'blar');
CREATE COLLATION blar2 (LC_COLLATE = 'blar');
CREATE COLLATION blar3 (LC_CTYPE = 'blar');
CREATE COLLATION blar4 (LC_COLLATE = 'blar', LC_CTYPE = 'blar');

-- test YB fr_FR.utf8 collation, unsupported locale name
CREATE COLLATION french1 (LOCALE = :fr_fr_collname);
CREATE COLLATION french2 (LC_COLLATE = :fr_fr_collname, LC_CTYPE = 'blar');
CREATE COLLATION french3 (LC_COLLATE = 'blar', LC_CTYPE = :fr_fr_collname);
CREATE COLLATION french4 (LC_COLLATE = :fr_fr_collname, LC_CTYPE = :fr_fr_collname);

-- test YB en_US.utf8 collation, supported locale name
CREATE COLLATION english1 (LOCALE = :en_us_collname);
CREATE COLLATION english2 (LC_COLLATE = :en_us_collname, LC_CTYPE = :en_us_collname);
CREATE COLLATION english3 (LC_COLLATE = 'POSIX', LC_CTYPE = :en_us_collname);
CREATE COLLATION english4 (LC_COLLATE = :en_us_collname, LC_CTYPE = 'POSIX');

-- test YB en_US.utf8 collation, unsupported locale name
CREATE COLLATION english5 (LC_COLLATE = :fr_fr_collname, LC_CTYPE = :en_us_collname);
CREATE COLLATION english6 (LC_COLLATE = :en_us_collname, LC_CTYPE = :fr_fr_collname);

-- test YB restrictions
\c yugabyte
SET search_path = collate_tests;
CREATE DATABASE test_db LC_COLLATE = "en-US-x-icu" TEMPLATE template0; -- fail;

CREATE TABLE tab3(id char(10) COLLATE "en-US-x-icu");
CREATE INDEX tab3_id_idx ON tab3(id bpchar_pattern_ops asc); -- fail;
\d tab3
CREATE INDEX tab3_id_idx ON tab3(id COLLATE "C" asc); -- ok;
\d tab3

CREATE TABLE tab4(id varchar(10) COLLATE "en-US-x-icu");
CREATE INDEX tab4_id_idx ON tab4(id varchar_pattern_ops asc); -- fail;
\d tab4
CREATE INDEX tab4_id_idx ON tab4(id COLLATE "C" asc); -- ok;
\d tab4

CREATE TABLE tab5(id text COLLATE "en-US-x-icu");
CREATE INDEX tab5_id_idx ON tab5(id text_pattern_ops asc); -- fail;
\d tab5
CREATE INDEX tab5_id_idx ON tab5(id COLLATE "C" asc); -- ok;
\d tab5

-- No index scan when collation does not match
CREATE TABLE collate_filter_pushdown (k text COLLATE "C", v text, PRIMARY KEY(k hash));
INSERT INTO collate_filter_pushdown (SELECT s, s FROM generate_series(1,1000) s);
EXPLAIN SELECT * from collate_filter_pushdown where k = 'A' COLLATE "C"; -- should push down filter and result in Index scan.
EXPLAIN SELECT * from collate_filter_pushdown where k = 'A' COLLATE "en_US"; -- should NOT push down filter and result in Seq scan.

-- test collation versions (to ensure that YB behaves as PG, update the logs
-- when merging with future PG release because it makes error case changes).
-- Create database without LOCALE/LC_COLLATE does not set datcollversion.
\c yugabyte
CREATE DATABASE test_db1;
CREATE DATABASE test_db2 TEMPLATE template0;
SELECT datcollversion IS NULL FROM pg_database WHERE datname = 'test_db1';
SELECT datcollversion IS NULL FROM pg_database WHERE datname = 'test_db2';

-- Create database LOCALE/LC_COLLATE will set datcollversion.
CREATE DATABASE test_db3 LOCALE 'en_US.UTF-8' TEMPLATE template0;
CREATE DATABASE test_db4 LC_COLLATE 'en_US.UTF-8' TEMPLATE template0;
SELECT datcollversion FROM pg_database WHERE datname = 'test_db3';
SELECT datcollversion FROM pg_database WHERE datname = 'test_db4';

-- By default collation version is auto-computed by PG from the libc or libicu libraries
-- provided by the underlining operating system.
-- Create database with a fake datcollversion 'blar' to test PG's collation version mismatch
-- warning mechanism. This simulate a situation where the DB was created using one version
-- of OS and after OS upgrade PG computes a new collation version.
CREATE DATABASE test_db5 LOCALE 'en_US.UTF-8' COLLATION_VERSION = 'blar' TEMPLATE template0;
SELECT datcollversion FROM pg_database WHERE datname = 'test_db5';
-- Should see a collation version mismatch warning.
\c test_db5
-- On seeing the collation version mismatch warning message, the user should rebuild all
-- the affected objects. After that we can refresh the database collation to get rid of
-- the collation version mismatch warning message.
ALTER DATABASE test_db5 REFRESH COLLATION VERSION;
SELECT datcollversion FROM pg_database WHERE datname = 'test_db5';
-- Should no longer see a collation version mismatch warning.
\c test_db5
\c yugabyte
-- Test collation version has no effect for C collation.
-- Should not see a collation version mismatch warning.
CREATE COLLATION test_coll1 (LOCALE = "C", VERSION = 'blar');
SELECT collversion FROM pg_collation WHERE collname = 'test_coll1';
CREATE TABLE test_tab1(id text COLLATE test_coll1);

-- PG 15 gives an error of "invalid collation version change", so does YB.
ALTER COLLATION test_coll1 REFRESH VERSION;

SELECT collversion FROM pg_collation WHERE collname = 'test_coll1';

-- Create a new collation with a fake version 'blar' to test PG's collation version mismatch
-- warning mechanism.
CREATE COLLATION test_coll2 (LOCALE = :en_us_collname, VERSION = 'blar');
SELECT collversion FROM pg_collation WHERE collname = 'test_coll2';
CREATE TABLE test_tab2(id text COLLATE test_coll2);
ALTER COLLATION test_coll2 REFRESH VERSION;
SELECT collversion FROM pg_collation WHERE collname = 'test_coll2';

-- Simulate OS upgrade that changed builtin collation version.
SET yb_test_collation = TRUE;
CREATE TABLE test_tab3(id text COLLATE "en-x-icu");
SELECT collversion FROM pg_collation WHERE collname = 'en-x-icu';
INSERT INTO test_tab3 VALUES ('aaa'), ('ZZZ');
SELECT id FROM test_tab3 ORDER BY id;
ALTER COLLATION "en-x-icu" REFRESH VERSION;
SELECT collversion FROM pg_collation WHERE collname = 'en-x-icu';
SELECT id FROM test_tab3 ORDER BY id;
