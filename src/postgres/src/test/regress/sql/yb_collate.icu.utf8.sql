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
