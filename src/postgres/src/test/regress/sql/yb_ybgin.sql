--
-- Yugabyte-owned test for ybgin access method.
--

-- Always choose index scan.
SET enable_seqscan = off;
SET enable_bitmapscan = on;
SET yb_test_ybgin_disable_cost_factor = 0.5;

--
-- Create non-temp table which uses Yugabyte storage.
--
CREATE TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE TABLE arrays (i serial PRIMARY KEY, a int[]);
CREATE TABLE jsonbs (i serial PRIMARY KEY, j jsonb);
CREATE TABLE multi (v tsvector, a1 text[], a2 float[], j1 jsonb, j2 jsonb);
CREATE TABLE expression (v tsvector, a text[], j jsonb);
CREATE TABLE partial (v tsvector, a text[], j jsonb);

--
-- tsvector
--
INSERT INTO vectors (v) VALUES
    (to_tsvector('simple', 'a bb ccc')),
    (to_tsvector('simple', 'bb a e i o u')),
    (to_tsvector('simple', 'ccd'));
CREATE INDEX ON vectors USING ybgin (v); -- test ambuild
INSERT INTO vectors (v) VALUES
    (to_tsvector('simple', 'a a a a a a')),
    (to_tsvector('simple', 'cc')); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a') ORDER BY i;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a') ORDER BY i;
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a & e') ORDER BY i;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a & e') ORDER BY i;
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb | cc') ORDER BY i;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb | cc') ORDER BY i;
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb & !ccc') ORDER BY i;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb & !ccc') ORDER BY i;
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'cc:*') ORDER BY i;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'cc:*') ORDER BY i;
EXPLAIN (costs off) SELECT count(*) FROM vectors WHERE v @@ to_tsquery('simple', 'a');
SELECT count(*) FROM vectors WHERE v @@ to_tsquery('simple', 'a');

--
-- array
--
INSERT INTO arrays (a) VALUES
    ('{1, 3, 5}'),
    (ARRAY[2, 3, 5]),
    ('{7, 1, 6, 4}');
CREATE INDEX ON arrays USING ybgin (a); -- test ambuild
INSERT INTO arrays (a) VALUES
    ('{3}'),
    ('{3, 3, 3}'),
    ('{10, 20, 30}'); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM arrays WHERE a && '{1, 100, 3}' ORDER BY i;
SELECT * FROM arrays WHERE a && '{1, 100, 3}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM arrays WHERE a @> '{5, 3}' ORDER BY i;
SELECT * FROM arrays WHERE a @> '{5, 3}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM arrays WHERE a <@ '{5, 4, 3, 2}' ORDER BY i;
SELECT * FROM arrays WHERE a <@ '{5, 4, 3, 2}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM arrays WHERE a = '{3}' ORDER BY i;
SELECT * FROM arrays WHERE a = '{3}' ORDER BY i;
EXPLAIN (costs off) SELECT count(*) FROM arrays WHERE a = '{3}';
SELECT count(*) FROM arrays WHERE a = '{3}';

--
-- jsonb
--
INSERT INTO jsonbs (j) VALUES
    ('{"aaa":[1,2.00,3]}'),
    ('{"ggg":"aaa"}'),
    ('["bbb", "aaa"]');
CREATE INDEX ON jsonbs USING ybgin (j); -- test ambuild
INSERT INTO jsonbs (j) VALUES
    ('"aaa"'),
    ('3.0'),
    ('{"aaa":{"bbb":[2,4], "ccc":{"ddd":6}}, "eee":8}'); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ? 'aaa' ORDER BY i;
SELECT * FROM jsonbs WHERE j ? 'aaa' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}' ORDER BY i;
SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}' ORDER BY i;
SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"bbb":[4]}' ORDER BY i;
SELECT * FROM jsonbs WHERE j @> '{"bbb":[4]}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}' ORDER BY i;
SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)' ORDER BY i;
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"' ORDER BY i;
SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"' ORDER BY i;
EXPLAIN (costs off) SELECT count(*) FROM jsonbs WHERE j ? 'aaa';
SELECT count(*) FROM jsonbs WHERE j ? 'aaa';

--
-- jsonb_path
--
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING ybgin (j jsonb_path_ops); -- test ambuild
INSERT INTO jsonbs (j) VALUES
    ('{"aaa":{"bbb":[2], "ccc":{"ddd":6}}, "eee":[]}'); -- test aminsert
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ? 'aaa' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}' ORDER BY i;
SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)' ORDER BY i;
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)' ORDER BY i;
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"' ORDER BY i;
SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"' ORDER BY i;

--
-- Multicolumn
--
INSERT INTO multi VALUES
    (to_tsvector('simple', 'a b'), ARRAY['c', 'd'], ARRAY[5.0, 6.1], '{"f":3}',
     '{"r":[3,6.5]}'),
    (to_tsvector('simple', '0'), ARRAY['0'], ARRAY[0], '0', '0');
CREATE INDEX ON multi USING ybgin (v, a1); -- test ambuild
CREATE INDEX ON multi USING ybgin (a2, a2); -- test ambuild
CREATE INDEX ON multi USING ybgin (j1 jsonb_ops, j2 jsonb_path_ops); -- test ambuild

--
-- Expression index
--
INSERT INTO expression VALUES
    (to_tsvector('simple', 'a b c'), ARRAY['d', 'e', 'f'], '{"g":["h","i"]}');
CREATE INDEX ON expression USING ybgin (tsvector_to_array(v)); -- test ambuild
CREATE INDEX ON expression USING ybgin (array_to_tsvector(a)); -- test ambuild
CREATE INDEX ON expression USING ybgin (
    jsonb_to_tsvector('simple', j, '["string"]')); -- test ambuild
INSERT INTO expression VALUES
    (to_tsvector('simple', 'a a'), ARRAY['d', 'd'], '{"g":"g"}'); -- test aminsert
EXPLAIN (costs off) SELECT count(*) FROM expression WHERE tsvector_to_array(v) && ARRAY['b'];
SELECT count(*) FROM expression WHERE tsvector_to_array(v) && ARRAY['b'];
EXPLAIN (costs off) SELECT count(*) FROM expression WHERE array_to_tsvector(a) @@ 'e';
SELECT count(*) FROM expression WHERE array_to_tsvector(a) @@ 'e';
EXPLAIN (costs off) SELECT count(*) FROM expression
    WHERE jsonb_to_tsvector('simple', j, '["string"]') @@ 'h';
SELECT count(*) FROM expression
    WHERE jsonb_to_tsvector('simple', j, '["string"]') @@ 'h';

--
-- Partial index
--
INSERT INTO partial VALUES
    (to_tsvector('simple', 'a a'), ARRAY['d', 'e', 'f'], '{"g":["h","i"]}');
CREATE INDEX ON partial USING ybgin (v) WHERE v @@ 'c'; -- test ambuild
CREATE INDEX ON partial USING ybgin (a) WHERE j @> '{"g":["i"]}'; -- test ambuild
CREATE INDEX ON partial USING ybgin (j) WHERE a && ARRAY['f']; -- test ambuild
INSERT INTO partial VALUES
    (to_tsvector('simple', 'a b c'), ARRAY['d', 'd'], '{"g":"g"}'); -- test aminsert
EXPLAIN (costs off) SELECT count(*) FROM partial WHERE v @@ 'b';
EXPLAIN (costs off) SELECT count(*) FROM partial WHERE v @@ 'c';
SELECT count(*) FROM partial WHERE v @@ 'c';
EXPLAIN (costs off) SELECT count(*) FROM partial WHERE a && ARRAY['e'];
EXPLAIN (costs off) SELECT count(*) FROM partial
    WHERE a && ARRAY['e'] and j @> '{"g":["i"]}';
SELECT count(*) FROM partial
    WHERE a && ARRAY['e'] and j @> '{"g":["i"]}';
EXPLAIN (costs off) SELECT count(*) FROM partial
    WHERE j @? '$.g[*] ? (@ == "h")';
EXPLAIN (costs off) SELECT count(*) FROM partial
    WHERE j @? '$.g[*] ? (@ == "h")' and a && ARRAY['f'];
SELECT count(*) FROM partial
    WHERE j @? '$.g[*] ? (@ == "h")' and a && ARRAY['f'];

-- Don't clean up the tables as they'll be used in later tests.

--
-- Try creating ybgin index on temp table.
--
CREATE TEMP TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE INDEX ON vectors USING ybgin (v);

-- Cleanup
DISCARD TEMP;
RESET enable_seqscan;
RESET enable_bitmapscan;
