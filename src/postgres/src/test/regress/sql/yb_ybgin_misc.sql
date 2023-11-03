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
-- Prefix match without greater string.
--
INSERT INTO vectors (v) VALUES
    (E'\x7F\x7F\x7F'),
    (E'\x7F\x7F\x7Fa'),
    (E'\x7F\x7F\x7F\x7F'),
    (E'\x7F\x7F');
EXPLAIN (costs off)
SELECT v FROM vectors WHERE v @@ E'\x7F\x7F\x7F:*' ORDER BY i;
SELECT v FROM vectors WHERE v @@ E'\x7F\x7F\x7F:*' ORDER BY i;
EXPLAIN (costs off)
DELETE FROM vectors WHERE v @@ E'\x7F:*';
DELETE FROM vectors WHERE v @@ E'\x7F:*';

--
-- Prefix match with other odd characters.
--
INSERT INTO vectors (v) VALUES
    (E'\x7F\u00bf'),
    (E'\x7F\u00c0'),
    (E'\x7F\u00cf'),
    (E'\x7F\u00d0'),
    (E'\x7F\u00df'),
    (E'\x7F\u00e0'),
    (E'\x7F\u00ef'),
    (E'\x7F\u00f0'),
    (E'\x7F\u00ff'),
    (E'\x7F\u0100'),
    (E'\x7F\u013f'),
    (E'\x7F\u0140'),
    (E'\x7F\u017f'),
    (E'\x7F\u0180'),
    (E'\x7F\u07ff'),
    (E'\x7F\u0800'),
    (E'\x7F\ufeff'),
    (E'\x7F\uff00'),
    (E'\x7F\uffff'),
    (E'\x7F\U00010000'),
    (E'\x7F\U0001ffff'),
    (E'\x7F\U00100000'),
    (E'\x7F\U0010ffff'),
    (E'\x7F\U0010ffff\x7F');
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u00bf:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u00cf:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u00df:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u00ef:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u00ff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u013f:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u017f:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\u07ff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\ufeff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\uffff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\U0001ffff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F\U0010ffff:*';
SELECT count(*) FROM vectors WHERE v @@ E'\x7F:*';
DELETE FROM vectors WHERE v @@ E'\x7F:*';

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
CREATE INDEX ON garrays USING ybgin (a);
INSERT INTO garrays (a) VALUES ('{22, 33}'), ('{33, 11}');
EXPLAIN (costs off)
SELECT * FROM garrays WHERE a && '{11}';
SELECT * FROM garrays WHERE a && '{11}';
-- Cleanup
DROP TABLE garrays;
DROP TABLEGROUP g;

--
-- ALTER TABLE ... COLUMN
--

-- Setup
CREATE TABLE altercoltab (a int[], i int);
CREATE INDEX NONCONCURRENTLY ON altercoltab USING ybgin (a);
INSERT INTO altercoltab VALUES ('{1}', 2);
-- Test
ALTER TABLE altercoltab DROP COLUMN i;
SELECT * FROM altercoltab WHERE a && '{1}';
ALTER TABLE altercoltab ADD COLUMN j int;
SELECT * FROM altercoltab WHERE a && '{1}';
ALTER TABLE altercoltab RENAME COLUMN j TO k;
SELECT * FROM altercoltab WHERE a && '{1}';
-- Cleanup
DROP TABLE altercoltab;
