/*
 * This test is for ICU collations.
 */

SET client_encoding TO UTF8;

CREATE SCHEMA collate_tests;
SET search_path = collate_tests;


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

SELECT * FROM collate_test1 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test2 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test3 WHERE b >= 'bbc' order by 1;
SELECT * FROM collate_test3 WHERE b >= 'BBC' order by 1;

SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' order by 1;
SELECT * FROM collate_test1 WHERE b >= 'bbc' COLLATE "C" order by 1;
SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "C" order by 1;
SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc' COLLATE "en-x-icu" order by 1;

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

CREATE DOMAIN testdomain_sv AS text COLLATE "sv-x-icu";
CREATE DOMAIN testdomain_i AS int COLLATE "sv-x-icu"; -- fails
CREATE TABLE collate_test4 (
    a int,
    b testdomain_sv
);
INSERT INTO collate_test4 SELECT * FROM collate_test1;
SELECT a, b FROM collate_test4 ORDER BY b;

CREATE TABLE collate_test5 (
    a int,
    b testdomain_sv COLLATE "en-x-icu"
);
INSERT INTO collate_test5 SELECT * FROM collate_test1;
SELECT a, b FROM collate_test5 ORDER BY b;


SELECT a, b FROM collate_test1 ORDER BY b;
SELECT a, b FROM collate_test2 ORDER BY b;
SELECT a, b FROM collate_test3 ORDER BY b;

SELECT a, b FROM collate_test1 ORDER BY b COLLATE "C";

-- star expansion
SELECT * FROM collate_test1 ORDER BY b;
SELECT * FROM collate_test2 ORDER BY b;
SELECT * FROM collate_test3 ORDER BY b;

-- constant expression folding
SELECT 'bbc' COLLATE "en-x-icu" > 'äbc' COLLATE "en-x-icu" AS "true";
SELECT 'bbc' COLLATE "sv-x-icu" > 'äbc' COLLATE "sv-x-icu" AS "false";

-- upper/lower

CREATE TABLE collate_test10 (
    a int,
    x text COLLATE "en-x-icu",
    y text COLLATE "tr-x-icu"
);

INSERT INTO collate_test10 VALUES (1, 'hij', 'hij'), (2, 'HIJ', 'HIJ');

SELECT a, lower(x), lower(y), upper(x), upper(y), initcap(x), initcap(y) FROM collate_test10 order by 1;
SELECT a, lower(x COLLATE "C"), lower(y COLLATE "C") FROM collate_test10 order by 1;

SELECT a, x, y FROM collate_test10 ORDER BY lower(y), a order by 1;

-- LIKE/ILIKE

SELECT * FROM collate_test1 WHERE b LIKE 'abc' order by 1;
SELECT * FROM collate_test1 WHERE b LIKE 'abc%' order by 1;
SELECT * FROM collate_test1 WHERE b LIKE '%bc%' order by 1;
SELECT * FROM collate_test1 WHERE b ILIKE 'abc' order by 1;
SELECT * FROM collate_test1 WHERE b ILIKE 'abc%' order by 1;
SELECT * FROM collate_test1 WHERE b ILIKE '%bc%' order by 1;

SELECT 'Türkiye' COLLATE "en-x-icu" ILIKE '%KI%' AS "true" order by 1;
SELECT 'Türkiye' COLLATE "tr-x-icu" ILIKE '%KI%' AS "false" order by 1;

SELECT 'bıt' ILIKE 'BIT' COLLATE "en-x-icu" AS "false" order by 1;
SELECT 'bıt' ILIKE 'BIT' COLLATE "tr-x-icu" AS "true" order by 1;

-- The following actually exercises the selectivity estimation for ILIKE.
SELECT relname FROM pg_class WHERE relname ILIKE 'abc%' order by 1;

-- regular expressions

SELECT * FROM collate_test1 WHERE b ~ '^abc$' order by 1;
SELECT * FROM collate_test1 WHERE b ~ '^abc' order by 1;
SELECT * FROM collate_test1 WHERE b ~ 'bc' order by 1;
SELECT * FROM collate_test1 WHERE b ~* '^abc$' order by 1;
SELECT * FROM collate_test1 WHERE b ~* '^abc' order by 1;
SELECT * FROM collate_test1 WHERE b ~* 'bc' order by 1;

CREATE TABLE collate_test6 (
    a int,
    b text COLLATE "en-x-icu"
);
INSERT INTO collate_test6 VALUES (1, 'abc'), (2, 'ABC'), (3, '123'), (4, 'ab1'),
                                 (5, 'a1!'), (6, 'a c'), (7, '!.;'), (8, '   '),
                                 (9, 'äbç'), (10, 'ÄBÇ');
SELECT b,
       b ~ '^[[:alpha:]]+$' AS is_alpha,
       b ~ '^[[:upper:]]+$' AS is_upper,
       b ~ '^[[:lower:]]+$' AS is_lower,
       b ~ '^[[:digit:]]+$' AS is_digit,
       b ~ '^[[:alnum:]]+$' AS is_alnum,
       b ~ '^[[:graph:]]+$' AS is_graph,
       b ~ '^[[:print:]]+$' AS is_print,
       b ~ '^[[:punct:]]+$' AS is_punct,
       b ~ '^[[:space:]]+$' AS is_space
FROM collate_test6 order by 1;

SELECT 'Türkiye' COLLATE "en-x-icu" ~* 'KI' AS "true" order by 1;
SELECT 'Türkiye' COLLATE "tr-x-icu" ~* 'KI' AS "true" order by 1;  -- true with ICU

SELECT 'bıt' ~* 'BIT' COLLATE "en-x-icu" AS "false" order by 1;
SELECT 'bıt' ~* 'BIT' COLLATE "tr-x-icu" AS "false" order by 1;  -- false with ICU

-- The following actually exercises the selectivity estimation for ~*.
SELECT relname FROM pg_class WHERE relname ~* '^abc' order by 1;


/* not run by default because it requires tr_TR system locale
-- to_char

SET lc_time TO 'tr_TR';
SELECT to_char(date '2010-04-01', 'DD TMMON YYYY');
SELECT to_char(date '2010-04-01', 'DD TMMON YYYY' COLLATE "tr-x-icu");
*/


-- backwards parsing

CREATE VIEW collview1 AS SELECT * FROM collate_test1 WHERE b COLLATE "C" >= 'bbc';
CREATE VIEW collview2 AS SELECT a, b FROM collate_test1 ORDER BY b COLLATE "C";
CREATE VIEW collview3 AS SELECT a, lower((x || x) COLLATE "C") FROM collate_test10;

SELECT table_name, view_definition FROM information_schema.views
  WHERE table_name LIKE 'collview%' ORDER BY 1;


-- collation propagation in various expression types

SELECT a, coalesce(b, 'foo') FROM collate_test1 ORDER BY 2;
SELECT a, coalesce(b, 'foo') FROM collate_test2 ORDER BY 2;
SELECT a, coalesce(b, 'foo') FROM collate_test3 ORDER BY 2;
SELECT a, lower(coalesce(x, 'foo')), lower(coalesce(y, 'foo')) FROM collate_test10 order by 1;

SELECT a, b, greatest(b, 'CCC') FROM collate_test1 ORDER BY b;
SELECT a, b, greatest(b, 'CCC') FROM collate_test2 ORDER BY b;
SELECT a, b, greatest(b, 'CCC') FROM collate_test3 ORDER BY b;
SELECT a, x, y, lower(greatest(x, 'foo')), lower(greatest(y, 'foo')) FROM collate_test10 order by 1;

SELECT a, nullif(b, 'abc') FROM collate_test1 ORDER BY 2;
SELECT a, nullif(b, 'abc') FROM collate_test2 ORDER BY 2;
SELECT a, nullif(b, 'abc') FROM collate_test3 ORDER BY 2;
SELECT a, lower(nullif(x, 'foo')), lower(nullif(y, 'foo')) FROM collate_test10 order by 1;

SELECT a, CASE b WHEN 'abc' THEN 'abcd' ELSE b END FROM collate_test1 ORDER BY 2;
SELECT a, CASE b WHEN 'abc' THEN 'abcd' ELSE b END FROM collate_test2 ORDER BY 2;
SELECT a, CASE b WHEN 'abc' THEN 'abcd' ELSE b END FROM collate_test3 ORDER BY 2;

CREATE DOMAIN testdomain AS text;
SELECT a, b::testdomain FROM collate_test1 ORDER BY 2;
SELECT a, b::testdomain FROM collate_test2 ORDER BY 2;
SELECT a, b::testdomain FROM collate_test3 ORDER BY 2;
SELECT a, b::testdomain_sv FROM collate_test3 ORDER BY 2;
SELECT a, lower(x::testdomain), lower(y::testdomain) FROM collate_test10 order by 1;

SELECT min(b), max(b) FROM collate_test1 order by 1;
SELECT min(b), max(b) FROM collate_test2 order by 1;
SELECT min(b), max(b) FROM collate_test3 order by 1;

SELECT array_agg(b ORDER BY b) FROM collate_test1 order by 1;
SELECT array_agg(b ORDER BY b) FROM collate_test2 order by 1;
SELECT array_agg(b ORDER BY b) FROM collate_test3 order by 1;

SELECT a, b FROM collate_test1 UNION ALL SELECT a, b FROM collate_test1 ORDER BY 2;
SELECT a, b FROM collate_test2 UNION SELECT a, b FROM collate_test2 ORDER BY 2;
SELECT a, b FROM collate_test3 WHERE a < 4 INTERSECT SELECT a, b FROM collate_test3 WHERE a > 1 ORDER BY 2;
SELECT a, b FROM collate_test3 EXCEPT SELECT a, b FROM collate_test3 WHERE a < 2 ORDER BY 2;

SELECT a, b FROM collate_test1 UNION ALL SELECT a, b FROM collate_test3 ORDER BY 2; -- fail
SELECT a, b FROM collate_test1 UNION ALL SELECT a, b FROM collate_test3 order by 2; -- ok
SELECT a, b FROM collate_test1 UNION SELECT a, b FROM collate_test3 ORDER BY 2; -- fail
SELECT a, b COLLATE "C" FROM collate_test1 UNION SELECT a, b FROM collate_test3 ORDER BY 2; -- ok
SELECT a, b FROM collate_test1 INTERSECT SELECT a, b FROM collate_test3 ORDER BY 2; -- fail
SELECT a, b FROM collate_test1 EXCEPT SELECT a, b FROM collate_test3 ORDER BY 2; -- fail

CREATE TABLE test_u AS SELECT a, b FROM collate_test1 UNION ALL SELECT a, b FROM collate_test3; -- fail

-- ideally this would be a parse-time error, but for now it must be run-time:
select x < y from collate_test10; -- fail
select x || y from collate_test10 order by 1; -- ok, because || is not collation aware
select x, y from collate_test10 order by x || y; -- not so ok

-- collation mismatch between recursive and non-recursive term
WITH RECURSIVE foo(x) AS
   (SELECT x FROM (VALUES('a' COLLATE "en-x-icu"),('b')) t(x)
   UNION ALL
   SELECT (x || 'c') COLLATE "de-x-icu" FROM foo WHERE length(x) < 10)
SELECT * FROM foo order by 1;


-- casting

SELECT CAST('42' AS text COLLATE "C") order by 1;

SELECT a, CAST(b AS varchar) FROM collate_test1 ORDER BY 2;
SELECT a, CAST(b AS varchar) FROM collate_test2 ORDER BY 2;
SELECT a, CAST(b AS varchar) FROM collate_test3 ORDER BY 2;


-- propagation of collation in SQL functions (inlined and non-inlined cases)
-- and plpgsql functions too

CREATE FUNCTION mylt (text, text) RETURNS boolean LANGUAGE sql
    AS $$ select $1 < $2 $$;

CREATE FUNCTION mylt_noninline (text, text) RETURNS boolean LANGUAGE sql
    AS $$ select $1 < $2 limit 1 $$;

CREATE FUNCTION mylt_plpgsql (text, text) RETURNS boolean LANGUAGE plpgsql
    AS $$ begin return $1 < $2; end $$;

SELECT a.b AS a, b.b AS b, a.b < b.b AS lt,
       mylt(a.b, b.b), mylt_noninline(a.b, b.b), mylt_plpgsql(a.b, b.b)
FROM collate_test1 a, collate_test1 b
ORDER BY a.b, b.b;

SELECT a.b AS a, b.b AS b, a.b < b.b COLLATE "C" AS lt,
       mylt(a.b, b.b COLLATE "C"), mylt_noninline(a.b, b.b COLLATE "C"),
       mylt_plpgsql(a.b, b.b COLLATE "C")
FROM collate_test1 a, collate_test1 b
ORDER BY a.b, b.b;


-- collation override in plpgsql

CREATE FUNCTION mylt2 (x text, y text) RETURNS boolean LANGUAGE plpgsql AS $$
declare
  xx text := x;
  yy text := y;
begin
  return xx < yy;
end
$$;

SELECT mylt2('a', 'B' collate "en-x-icu") as t, mylt2('a', 'B' collate "C") as f;

CREATE OR REPLACE FUNCTION
  mylt2 (x text, y text) RETURNS boolean LANGUAGE plpgsql AS $$
declare
  xx text COLLATE "POSIX" := x;
  yy text := y;
begin
  return xx < yy;
end
$$;

SELECT mylt2('a', 'B') as f;
SELECT mylt2('a', 'B' collate "C") as fail; -- conflicting collations
SELECT mylt2('a', 'B' collate "POSIX") as f;


-- polymorphism

SELECT * FROM unnest((SELECT array_agg(b ORDER BY b) FROM collate_test1)) ORDER BY 1;
SELECT * FROM unnest((SELECT array_agg(b ORDER BY b) FROM collate_test2)) ORDER BY 1;
SELECT * FROM unnest((SELECT array_agg(b ORDER BY b) FROM collate_test3)) ORDER BY 1;

CREATE FUNCTION dup (anyelement) RETURNS anyelement
    AS 'select $1' LANGUAGE sql;

SELECT a, dup(b) FROM collate_test1 ORDER BY 2;
SELECT a, dup(b) FROM collate_test2 ORDER BY 2;
SELECT a, dup(b) FROM collate_test3 ORDER BY 2;


-- indexes

CREATE INDEX collate_test1_idx1 ON collate_test1 (b);
CREATE INDEX collate_test1_idx2 ON collate_test1 (b COLLATE "C");
CREATE INDEX collate_test1_idx3 ON collate_test1 ((b COLLATE "C")); -- this is different grammatically
CREATE INDEX collate_test1_idx4 ON collate_test1 (((b||'foo') COLLATE "POSIX"));

CREATE INDEX collate_test1_idx5 ON collate_test1 (a COLLATE "C"); -- fail
CREATE INDEX collate_test1_idx6 ON collate_test1 ((a COLLATE "C")); -- fail

SELECT relname, pg_get_indexdef(oid) FROM pg_class WHERE relname LIKE 'collate_test%_idx%' ORDER BY 1;


-- schema manipulation commands

CREATE ROLE regress_test_role;
CREATE SCHEMA test_schema;

-- We need to do this this way to cope with varying names for encodings:
do $$
BEGIN
  EXECUTE 'CREATE COLLATION test0 (provider = icu, locale = ' ||
          quote_literal(current_setting('lc_collate')) || ');';
END
$$;
CREATE COLLATION test0 FROM "C"; -- fail, duplicate name
do $$
BEGIN
  EXECUTE 'CREATE COLLATION test1 (provider = icu, lc_collate = ' ||
          quote_literal(current_setting('lc_ctype')) || -- use 'lc_ctype' instead of 'lc_collate' for YB
          ', lc_ctype = ' ||
          quote_literal(current_setting('lc_ctype')) || ');';
END
$$;
CREATE COLLATION test3 (provider = icu, lc_collate = 'en_US.utf8'); -- fail, need lc_ctype
CREATE COLLATION testx (provider = icu, locale = 'nonsense'); /* never fails with ICU */  DROP COLLATION testx;

CREATE COLLATION test4 FROM nonsense;
CREATE COLLATION test5 FROM test0;

SELECT collname FROM pg_collation WHERE collname LIKE 'test%' ORDER BY 1;

ALTER COLLATION test1 RENAME TO test11;
ALTER COLLATION test0 RENAME TO test11; -- fail
ALTER COLLATION test1 RENAME TO test22; -- fail

ALTER COLLATION test11 OWNER TO regress_test_role;
ALTER COLLATION test11 OWNER TO nonsense;
ALTER COLLATION test11 SET SCHEMA test_schema;

COMMENT ON COLLATION test0 IS 'US English';

SELECT collname, nspname, obj_description(pg_collation.oid, 'pg_collation')
    FROM pg_collation JOIN pg_namespace ON (collnamespace = pg_namespace.oid)
    WHERE collname LIKE 'test%'
    ORDER BY 1;

DROP COLLATION test0, test_schema.test11, test5; -- fail in YB
DROP COLLATION test0;
DROP COLLATION test_schema.test11;
DROP COLLATION test5;
DROP COLLATION test0; -- fail
DROP COLLATION IF EXISTS test0;

SELECT collname FROM pg_collation WHERE collname LIKE 'test%';

DROP SCHEMA test_schema;
DROP ROLE regress_test_role;


-- ALTER

ALTER COLLATION "en-x-icu" REFRESH VERSION;


-- dependencies

CREATE COLLATION test0 FROM "C";

CREATE TABLE collate_dep_test1 (a int, b text COLLATE test0);
CREATE DOMAIN collate_dep_dom1 AS text COLLATE test0;
CREATE TYPE collate_dep_test2 AS (x int, y text COLLATE test0);
CREATE VIEW collate_dep_test3 AS SELECT text 'foo' COLLATE test0 AS foo;
CREATE TABLE collate_dep_test4t (a int, b text);
CREATE INDEX collate_dep_test4i ON collate_dep_test4t (b COLLATE test0);

\set VERBOSITY terse \\ -- suppress cascade details
DROP COLLATION test0 RESTRICT; -- fail
SET client_min_messages = WARNING; -- suppress cascading notice
DROP COLLATION test0 CASCADE;
SET client_min_messages = NOTICE;

\d collate_dep_test1
\d collate_dep_test2

DROP TABLE collate_dep_test1, collate_dep_test4t;
DROP TYPE collate_dep_test2;

-- test range types and collations

create type textrange_c as range(subtype=text, collation="C");
create type textrange_en_us as range(subtype=text, collation="en-x-icu");

select textrange_c('A','Z') @> 'b'::text;
select textrange_en_us('A','Z') @> 'b'::text;

drop type textrange_c;
drop type textrange_en_us;

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

ALTER TABLE tab1 ALTER COLUMN id SET DATA TYPE varchar(5) collate "en-US-x-icu"; -- rewrite
\d tab1

CREATE TABLE tab2(id varchar(10) collate "en-US-x-icu");
CREATE INDEX tab2_id_idx on tab2(id collate "C" desc);
INSERT INTO tab2 VALUES ('aaaa');
\d tab2
ALTER TABLE tab2 alter COLUMN id SET DATA TYPE varchar(20) collate "en-US-x-icu"; -- no rewrite
\d tab2

-- test YB restrictions
CREATE DATABASE test_db LC_COLLATE = "en-US-x-icu" TEMPLATE template0; -- fail;

CREATE TABLE tab3(id char(10) collate "en-US-x-icu");
CREATE INDEX tab3_id_idx ON tab3(id bpchar_pattern_ops asc); -- fail;
\d tab3
CREATE INDEX tab3_id_idx ON tab3(id collate "C" asc); -- ok;
\d tab3

CREATE TABLE tab4(id varchar(10) collate "en-US-x-icu");
CREATE INDEX tab4_id_idx ON tab4(id varchar_pattern_ops asc); -- fail;
\d tab4
CREATE INDEX tab4_id_idx ON tab4(id collate "C" asc); -- ok;
\d tab4

CREATE TABLE tab5(id text collate "en-US-x-icu");
CREATE INDEX tab5_id_idx ON tab5(id text_pattern_ops asc); -- fail;
\d tab5
CREATE INDEX tab5_id_idx ON tab5(id collate "C" asc); -- ok;
\d tab5

-- No index scan when collation does not match
CREATE TABLE collate_filter_pushdown (k text collate "C", v text, PRIMARY KEY(k hash));
INSERT INTO collate_filter_pushdown (SELECT s, s FROM generate_series(1,1000) s);
EXPLAIN SELECT * from collate_filter_pushdown where k = 'A' collate "C"; -- should push down filter and result in Index scan.
EXPLAIN SELECT * from collate_filter_pushdown where k = 'A' collate "en_US"; -- should NOT push down filter and result in Seq scan.

-- cleanup
SET client_min_messages = WARNING; -- suppress cascading notice
DROP SCHEMA collate_tests CASCADE;
SET client_min_messages = NOTICE;
RESET search_path;

-- leave a collation for pg_upgrade test
CREATE COLLATION coll_icu_upgrade FROM "und-x-icu";
