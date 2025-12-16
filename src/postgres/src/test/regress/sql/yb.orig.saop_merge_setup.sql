--
-- See yb_saop_merge_schedule for details about the test.
-- This is the setup to load the data used in tests later in the schedule.
--

--
-- Range
--
CREATE TABLE r5n (
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + r4 * 1000 + r5 * 10000)::int) STORED,
    r5 float8,
    r3 int,
    r1 int2,
    r2 int8,
    r4 numeric,
    PRIMARY KEY (r1 ASC, r2, r3, r4, r5))
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (2, 2, 2, 2, 2),
    (3));
WITH g(i) AS (
    SELECT generate_series(0, 9)
), rows AS (
    INSERT INTO r5n (r1, r2, r3, r4, r5)
    SELECT a.i, b.i, c.i, d.i, e.i
    FROM g a, g b, g c, g d, g e
    WHERE yb_hash_code(a.i, b.i, c.i, d.i, e.i) / 65535::float < 0.05
    RETURNING 1
) SELECT count(*) FROM rows;

--
-- Hash
--
CREATE TABLE h3r2n (
    n int GENERATED ALWAYS AS ((h1 + h2 * 10 + h3 * 100 + r1 * 1000 + r2 * 10000)::int) STORED,
    r1 int2,
    r2 float8,
    h1 int2,
    h2 int8,
    h3 int,
    PRIMARY KEY ((h1, h2, h3) HASH, r1, r2))
SPLIT INTO 9 TABLETS;
INSERT INTO h3r2n (h1, h2, h3, r1, r2) SELECT r1, r2, r3, r4, r5 FROM r5n;

--
-- TODO(#9958): when btree_gin is supported, test it.
--

--
-- Partitioned table
--
CREATE TABLE parent (
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    r3 int,
    p1 int2,
    r1 int2,
    r2 int8,
    p2 float8,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2))
PARTITION BY RANGE (p1, p2);
CREATE TABLE child1 (
    p1 int2,
    r3 int,
    r1 int2,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    r2 int8,
    p2 float8,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2))
PARTITION BY RANGE (p2, p1);
CREATE TABLE child1a (
    r1 int2,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    r2 int8,
    p2 float8,
    p1 int2,
    r3 int,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2));
CREATE TABLE child1b (
    r2 int8,
    r3 int,
    p2 float8,
    p1 int2,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    r1 int2,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2));
CREATE TABLE child2 (
    r2 int8,
    r1 int2,
    p1 int2,
    p2 float8,
    r3 int,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2));
CREATE TABLE child3 (
    r3 int,
    r1 int2,
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + p1 * 1000 + p2 * 10000)::int) STORED,
    p2 float8,
    r2 int8,
    p1 int2,
    PRIMARY KEY (r1 ASC, r2, r3, p1, p2));
ALTER TABLE parent ATTACH PARTITION child1 FOR VALUES FROM (0, 0) TO (3, 6);
ALTER TABLE child1 ATTACH PARTITION child1a FOR VALUES FROM (0, 0) TO (5, 2);
ALTER TABLE child1 ATTACH PARTITION child1b FOR VALUES FROM (5, 2) TO (maxvalue, maxvalue);
ALTER TABLE parent ATTACH PARTITION child2 FOR VALUES FROM (3, 6) TO (7, 4);
ALTER TABLE parent ATTACH PARTITION child3 FOR VALUES FROM (7, 4) TO (9, 10);
INSERT INTO parent (r1, r2, r3, p1, p2) SELECT r1, r2, r3, r4, r5 FROM r5n;

--
-- Bucketed PK
--
CREATE TABLE bkt_tbl (
    n int GENERATED ALWAYS AS ((r1 + r2 * 10 + r3 * 100 + r4 * 1000 + r5 * 10000)::int) STORED,
    r5 float8,
    r3 int,
    r1 int2,
    r2 int8,
    r4 numeric,
    bkt int GENERATED ALWAYS AS (yb_hash_code(r3, r2, r4, r5, r1) % 3) STORED,
    PRIMARY KEY (bkt ASC, r1, r2, r3, r4, r5))
SPLIT AT VALUES (
    (1),
    (2),
    (2, 2),
    (2, 2, 2),
    (2, 2, 2, 2),
    (2, 2, 2, 2, 2),
    (3));
INSERT INTO bkt_tbl (r1, r2, r3, r4, r5) SELECT r1, r2, r3, r4, r5 FROM r5n;

--
-- Analyze
--
ANALYZE r5n, h3r2n, parent, bkt_tbl;
