--
-- Yugabyte-owned test on ybgin index access method for things not covered
-- elsewhere.
--

-- Disable sequential scan so that index scan is always chosen.
SET enable_seqscan = off;

-- Set jsonbs to have jsonb_ops index, not jsonb_path_ops index.
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING ybgin (j);

--
-- DELETE/UPDATE
--

-- tsvector
INSERT INTO vectors (v) VALUES ('a b c i k'), ('abc j k');
EXPLAIN (costs off)
UPDATE vectors SET v = ts_delete(v, '{c, j}'::text[]) WHERE v @@ 'k';
UPDATE vectors SET v = ts_delete(v, '{c, j}'::text[]) WHERE v @@ 'k';
SELECT v FROM vectors WHERE v @@ 'k' ORDER BY i;
DELETE FROM vectors WHERE v @@ 'k';
-- jsonb
INSERT INTO jsonbs (j) VALUES ('[4,7,"sh",3,"du"]'), ('{"du":[7,9], "sh":13}');
EXPLAIN (costs off)
UPDATE jsonbs SET j = j - 'sh' WHERE j ? 'du';
UPDATE jsonbs SET j = j - 'sh' WHERE j ? 'du';
SELECT j FROM jsonbs WHERE j ? 'du' ORDER BY i;
DELETE FROM jsonbs WHERE j ? 'du';

--
-- Long strings.
--
DO $$
DECLARE
    c int;
    s text := '0000000000'
           || '1111111111'
           || '2222222222'
           || '3333333333'
           || '4444444444'
           || '5555555555'
           || '6666666666'
           || '7777777777'
           || '8888888888'
           || '9999999999'
           || 'aaaaaaaaaa'
           || 'bbbbbbbbbb'
           || 'cccccccccc'
           || 'dddddddddd'
           || 'eeeeeeeeee'
           || 'ffffffffff';
BEGIN
    -- tsvector_ops
    INSERT INTO vectors (v) VALUES (to_tsvector('simple', s));
    SELECT count(*) FROM vectors WHERE v @@ to_tsquery('simple', s) INTO c;
    RAISE NOTICE 'tsvector_ops count: %', c;
    DELETE FROM vectors WHERE v @@ to_tsquery('simple', s);
    -- jsonb_ops
    INSERT INTO jsonbs (j) VALUES (('"' || s || '"')::jsonb);
    SELECT count(*) FROM jsonbs WHERE j ? s INTO c;
    RAISE NOTICE 'jsonb_ops count: %', c;
    DELETE FROM jsonbs WHERE j ? s;
END $$;

--
-- Three ways to query tsvectors containing both of two specific words.
--

-- AND inside tsquery
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a & bb';
SELECT * FROM vectors WHERE v @@ 'a & bb';
-- AND between '@@' ops
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a' and v @@ 'bb';
SELECT * FROM vectors WHERE v @@ 'a' and v @@ 'bb';
-- INTERSECT between '@@' selects
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a' INTERSECT SELECT * FROM vectors WHERE v @@ 'bb';
SELECT * FROM vectors WHERE v @@ 'a' INTERSECT SELECT * FROM vectors WHERE v @@ 'bb';

--
-- Three ways to query tsvectors containing at least one of two specific words.
--

-- OR inside tsquery
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a | c';
SELECT * FROM vectors WHERE v @@ 'a | c';
-- OR between '@@' ops
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a' or v @@ 'c';
SELECT * FROM vectors WHERE v @@ 'a' or v @@ 'c';
-- UNION between '@@' selects
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'a' UNION SELECT * FROM vectors WHERE v @@ 'c';
SELECT * FROM vectors WHERE v @@ 'a' UNION SELECT * FROM vectors WHERE v @@ 'c';

--
-- Complicated query
--

-- Top-level disjunction where the last item is simple
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ '((a & e) | (ccc & ccd)) & o';
SELECT * FROM vectors WHERE v @@ '((a & e) | (ccc & ccd)) & o';
-- Top-level disjunction where the last item is complex
EXPLAIN (costs off)
SELECT * FROM vectors WHERE v @@ 'o & ((a & e) | (ccc & ccd))';
SELECT * FROM vectors WHERE v @@ 'o & ((a & e) | (ccc & ccd))';
-- TODO(jason): add top-level conjunction tests

--
-- Index build without online schema change
--
CREATE INDEX NONCONCURRENTLY idx_nonconcurrent ON jsonbs USING ybgin (j jsonb_path_ops);
DROP INDEX idx_nonconcurrent;

--
-- Various array types
--
CREATE TABLE manyarrays (
    a text[],
    b int2[],
    c char[],
    d oidvector[],
    e pg_lsn[],
    f timestamptz[]);
INSERT INTO manyarrays VALUES (
    ARRAY['abc', '123'],
    ARRAY[123, -456],
    ARRAY['g', 'h', 'i'],
    ARRAY['123456 0', '4294967295', '1 2 4']::oidvector[],
    ARRAY['0/12345', 'AC/FD', '0/0']::pg_lsn[],
    ARRAY[make_timestamptz(1970, 1, 2, 3, 4, 5.6, 'UTC'),
          make_timestamptz(1971, 2, 3, 4, 5, 6.7, 'UTC')]);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (a);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (b);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (c);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (d);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (e);
CREATE INDEX NONCONCURRENTLY ON manyarrays USING ybgin (f);

--
-- Column ordering
--

-- ASC
CREATE INDEX idx_hash ON jsonbs USING ybgin (j ASC);
-- DESC
CREATE INDEX idx_hash ON jsonbs USING ybgin (j DESC);
-- HASH
CREATE INDEX idx_hash ON jsonbs USING ybgin (j HASH);
-- NULLS FIRST
CREATE INDEX idx_hash ON jsonbs USING ybgin (j NULLS FIRST);
-- NULLS LAST
CREATE INDEX idx_hash ON jsonbs USING ybgin (j NULLS LAST);

--
-- Colocation
--

-- Setup
CREATE TABLEGROUP g;
-- Colocated table and index
CREATE TABLE garrays (i serial PRIMARY KEY, a int[]) TABLEGROUP g;
INSERT INTO garrays (a) VALUES ('{11, 22}');
CREATE INDEX ON garrays USING ybgin (a) TABLEGROUP g;
INSERT INTO garrays (a) VALUES ('{22, 33}'), ('{33, 11}');
EXPLAIN
SELECT * FROM garrays WHERE a && '{11}';
SELECT * FROM garrays WHERE a && '{11}';
-- Noncolocated table and colocated index
CREATE TABLE nogarrays (i serial PRIMARY KEY, a int[]);
INSERT INTO nogarrays (a) VALUES ('{11, 22}');
CREATE INDEX ON nogarrays USING ybgin (a) TABLEGROUP g;
INSERT INTO nogarrays (a) VALUES ('{22, 33}'), ('{33, 11}');
EXPLAIN
SELECT * FROM nogarrays WHERE a && '{11}';
SELECT * FROM nogarrays WHERE a && '{11}';
-- Cleanup
DROP TABLE garrays, nogarrays;
DROP TABLEGROUP g;
