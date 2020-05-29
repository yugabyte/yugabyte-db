--------------------------------------
-- Set up tables.
--------------------------------------

CREATE TABLE t1(h int, r int, v1 int, v2 int, v3 int, primary key(h HASH, r ASC));
CREATE INDEX t1_v1_v2_idx on t1(v1 HASH, v2 ASC);
CREATE UNIQUE INDEX t1_v3_uniq_idx on t1(v3 HASH);

CREATE TABLE t2(h int, r int, v1 int, v2 int, v3 int, primary key(h HASH, r ASC));
CREATE INDEX t2_v1_v2_idx on t2(v1 HASH, v2 ASC);
CREATE UNIQUE INDEX t2_v3_uniq_idx on t2(v3 HASH);

CREATE TABLE t3(h int, r int, v1 int, v2 int, v3 int, primary key(h ASC, r ASC));
CREATE INDEX t3_v1_v2_idx on t3(v1 ASC, v2 ASC);
CREATE UNIQUE INDEX t3_v3_uniq_idx on t3(v3 ASC);

-- Should make use of eq transitivity and use pkey on both tables.
EXPLAIN SELECT *
FROM t1
     JOIN t2 on t1.h = t2.h and t1.r = t2.r
WHERE t1.h = 2 and t2.r = 3;

-- Should make use of eq transitivity and use full pkey on t2 and partial pkey on t1.
EXPLAIN SELECT *
FROM t1
     JOIN t2 on t1.h = t2.h
WHERE t1.h = 2 and t2.r = 3;

-- Should use pkey index on t1 (a) and t2_v1_v2_idx (due to join condition).
EXPLAIN SELECT *
FROM t1 as a
     JOIN t1 as b on a.h = b.v1
WHERE a.h = 2 and a.r = 3;

-- Should make use of eq transitivity and use pkey on all 3 tables (then sort 1 row in memory).
EXPLAIN SELECT *
FROM t1
     JOIN t2 on t1.h = t2.h and t1.r = t2.r
     JOIN t3 on t2.h = t3.h and t1.r = t3.r
WHERE t1.h = 1 and t3.r = 2
ORDER BY t3.v3 DESC;

-- Should use v3_uniq_idx on t3 and t2 and v1_v2_idx (on partial key) for t1.
EXPLAIN SELECT *
FROM t1
     JOIN t2 on t1.v1 = t2.v1
     JOIN t3 on t2.v3 = t3.v3
WHERE t1.h = 1 and t3.v3 = 2
ORDER BY t3.v3 ASC;

-- Should still use same indexes as above, only t3.v1 > 5 condition for filtering.
EXPLAIN SELECT *
FROM t1
     JOIN t2 on t1.v1 = t2.v1
     JOIN t3 on t2.v3 = t3.v3
WHERE t1.h = 1 and t3.v3 = 2 and t3.v1 > 5
ORDER BY t3.v3 ASC;

-- Should use pkey on t1, then v3_uniq_idx on t2 and v1_v2_idx on t3.
EXPLAIN SELECT *
FROM t1
     FULL JOIN t2 on t1.v3 = t2.v3
     FULL JOIN t3 on t1.v1 = t3.v1 and t3.v1 = t1.v1
WHERE t1.h = 1 and t1.r = 2
ORDER BY t3.v3 DESC;

-- Should still use same indexes as above, only use t2.r IN condition for filtering.
EXPLAIN SELECT *
FROM t1
     FULL JOIN t2 on t1.v3 = t2.v3
     FULL JOIN t3 on t1.v1 = t3.v1 and t3.v1 = t1.v1
WHERE t1.h = 1 and t1.r = 2 and t2.r IN (3,4,5)
ORDER BY t3.v3 DESC;

-- Should still use same indexes as above, but use the IN condition on v2 for t3.v1_v2_idx.
EXPLAIN SELECT *
FROM t1
     FULL JOIN t2 on t1.v3 = t2.v3
     FULL JOIN t3 on t1.v1 = t3.v1 and t3.v1 = t1.v1
WHERE t1.h = 1 and t1.r = 2 and t3.v2 IN (3,4,5)
ORDER BY t3.v3 DESC;
