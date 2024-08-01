--
-- SELECT
--

CREATE TABLE nr (i int, j int, PRIMARY KEY (j ASC));
CREATE INDEX ON nr (i ASC);
INSERT INTO nr VALUES (1, 2), (3, 4);
BEGIN;
INSERT INTO nr VALUES (null, 6);
SELECT i FROM nr ORDER BY i;
SELECT i FROM nr ORDER BY i NULLS FIRST;
SELECT i FROM nr ORDER BY i DESC;
SELECT i FROM nr ORDER BY i DESC NULLS LAST;
ROLLBACK;

CREATE TABLE nr2 (i int, j int, PRIMARY KEY (j ASC));
CREATE INDEX ON nr2 (i DESC);
INSERT INTO nr2 VALUES (1, 2), (3, 4);
BEGIN;
INSERT INTO nr2 VALUES (null, 6);
SELECT i FROM nr2 ORDER BY i;
SELECT i FROM nr2 ORDER BY i NULLS FIRST;
SELECT i FROM nr2 ORDER BY i DESC;
SELECT i FROM nr2 ORDER BY i DESC NULLS LAST;
ROLLBACK;

--
-- Test reverse scans with limit.
--
CREATE TABLE reverse_scan_test (
  h BIGINT,
  r INT,
  PRIMARY KEY(h, r ASC)
);

INSERT INTO reverse_scan_test
VALUES (1, 1),
       (1, 2),
       (1, 3),
       (1, 4),
       (1, 5),
       (1, 6),
       (1, 7),
       (1, 8),
       (1, 9),
       (1, 10),
       (1, 11),
       (1, 12),
       (1, 13),
       (1, 14);

-- Check ascending order.
SELECT r FROM reverse_scan_test WHERE h = 1;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint);

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 2 and r < 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9 LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 2 and r < 9 LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 2 and r <= 13;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13 LIMIT 10;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 2 and r <= 13 LIMIT 10;

-- Test desc order (reverse scan).
SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r < 9 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r < 9 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 1 AND r < 14 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC LIMIT 9;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 1 AND r < 14 ORDER BY r DESC LIMIT 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 3 and r <= 13 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC LIMIT 8;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 3 and r <= 13 ORDER BY r DESC LIMIT 8;

PREPARE myplan AS SELECT * FROM reverse_scan_test LIMIT $1;

-- Execute myplan > 5 times. After 5 times the plan should default to the
-- generic plan
EXECUTE myplan(0);
EXPLAIN EXECUTE myplan(null);
EXECUTE myplan(1);
EXECUTE myplan(2);
EXECUTE myplan(3);
-- generic plan with limit node should be used
EXPLAIN EXECUTE myplan(null);
EXECUTE myplan(null);
EXECUTE myplan(0);
EXECUTE myplan(1);

--
-- For https://github.com/YugaByte/yugabyte-db/issues/10254
--
CREATE TABLE t(h INT, r INT, PRIMARY KEY(h, r ASC));
INSERT INTO t VALUES(1, 1), (1, 3);
SELECT * FROM t WHERE h = 1 AND r in(1, 3) FOR KEY SHARE;
-- On this query postgres process stucked in an infinite loop.
SELECT * FROM t WHERE h = 1 AND r IN (1, 2, 3) FOR KEY SHARE;

-- Testing distinct pushdown, see #16552
-- TODO(tanuj): add back ANALYZE when #16633 is fixed.
EXPLAIN (SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT DISTINCT att.attname as name, att.attnum as OID, pg_catalog.format_type(ty.oid,NULL) AS datatype,
	att.attnotnull as not_null, att.atthasdef as has_default_val
	FROM pg_catalog.pg_attribute att
	    JOIN pg_catalog.pg_type ty ON ty.oid=atttypid
	WHERE
	    att.attnum > 0
	    AND att.attisdropped IS FALSE
	ORDER BY att.attnum LIMIT 1;

-- check system columns in YbSeqScan(#18485)
explain (costs off)
/*+ SeqScan(nr2) */ SELECT tableoid::regclass, * from nr2 where i = 1;
/*+ SeqScan(nr2) */ SELECT tableoid::regclass, * from nr2 where i = 1;

--
-- For https://github.com/YugaByte/yugabyte-db/issues/20316
--
create table a (a int);
insert into a values (1),(2),(3),(4);

create table b (b int, a int);
insert into b values (1, 1),(2, 1),(3, 2),(4, 4);

create table c (c int);
insert into c values (1),(2),(3);

create table d (b int, c int);
insert into d values (1, 1),(1, 2),(1, 3),(2, 1),(3, 2),(3, 3);

explain (verbose, costs off)
select
  a.a,
  (
    select sum(t.v0)
    from (
      select c.c as v0
      from c
      where c.c in (
        select d.c
        from d
          join b
            on b.b = d.b
        where b.a = a.a
      )
    ) as t
  )
from a
order by a.a;
select
  a.a,
  (
    select sum(t.v0)
    from (
      select c.c as v0
      from c
      where c.c in (
        select d.c
        from d
          join b
            on b.b = d.b
        where b.a = a.a
      )
    ) as t
  )
from a
order by a.a;

explain (verbose, costs off)
select
  a.a,
  (
    select sum(t.v0)
    from (
      select c.c as v0
      from c
      where c.c in (
        select d.c
        from d
          join b
            on b.b = d.b
        where b.a = a.a
      )
    ) as t
  )
from a
where a in (1, 2)
order by a.a;
select
  a.a,
  (
    select sum(t.v0)
    from (
      select c.c as v0
      from c
      where c.c in (
        select d.c
        from d
          join b
            on b.b = d.b
        where b.a = a.a
      )
    ) as t
  )
from a
where a in (1, 2)
order by a.a;

-- #23287 string_to_text pushdown
create table string_to_text_test(data text);
insert into string_to_text_test values ('foo bar');
explain (costs off)
select * from string_to_text_test where 'foo' = ANY(string_to_array(data, ' '));
select * from string_to_text_test where 'foo' = ANY(string_to_array(data, ' '));
drop table string_to_text_test;
