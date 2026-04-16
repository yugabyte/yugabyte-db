--
-- Yugabyte-owned test for gin access method.
--

-- Disable sequential scan so that bitmap index scan is always chosen.
SET enable_seqscan = off;
SET enable_bitmapscan = on;

--
-- Create temp tables because gin access method is only supported on temporary
-- tables.
--
CREATE TEMP TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE TEMP TABLE arrays (i serial PRIMARY KEY, a int[]);
CREATE TEMP TABLE jsonbs (i serial PRIMARY KEY, j jsonb);
CREATE TEMP TABLE multi (v tsvector, a1 text[], a2 float[], j1 jsonb, j2 jsonb);
CREATE TEMP TABLE expression (v tsvector, a text[], j jsonb);
CREATE TEMP TABLE partial (v tsvector, a text[], j jsonb);

--
-- tsvector
--
INSERT INTO vectors (v) VALUES
    (to_tsvector('simple', 'a bb ccc')),
    (to_tsvector('simple', 'bb a e i o u')),
    (to_tsvector('simple', 'ccd'));
CREATE INDEX ON vectors USING gin (v); -- test ambuild
INSERT INTO vectors (v) VALUES
    (to_tsvector('simple', 'a a a a a a')),
    (to_tsvector('simple', 'cc')); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a');
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a & e');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'a & e');
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb | cc');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb | cc');
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb & !ccc');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'bb & !ccc');
EXPLAIN (costs off) SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'cc:*');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'cc:*');
EXPLAIN (costs off) SELECT count(*) FROM vectors WHERE v @@ to_tsquery('simple', 'a');
SELECT count(*) FROM vectors WHERE v @@ to_tsquery('simple', 'a');

--
-- array
--
INSERT INTO arrays (a) VALUES
    ('{1, 3, 5}'),
    (ARRAY[2, 3, 5]),
    ('{7, 1, 6, 4}');
CREATE INDEX ON arrays USING gin (a); -- test ambuild
INSERT INTO arrays (a) VALUES
    ('{3}'),
    ('{3, 3, 3}'),
    ('{10, 20, 30}'); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM arrays WHERE a && '{1, 100, 3}';
SELECT * FROM arrays WHERE a && '{1, 100, 3}';
EXPLAIN (costs off) SELECT * FROM arrays WHERE a @> '{5, 3}';
SELECT * FROM arrays WHERE a @> '{5, 3}';
EXPLAIN (costs off) SELECT * FROM arrays WHERE a <@ '{5, 4, 3, 2}';
SELECT * FROM arrays WHERE a <@ '{5, 4, 3, 2}';
EXPLAIN (costs off) SELECT * FROM arrays WHERE a = '{3}';
SELECT * FROM arrays WHERE a = '{3}';
EXPLAIN (costs off) SELECT count(*) FROM arrays WHERE a = '{3}';
SELECT count(*) FROM arrays WHERE a = '{3}';

--
-- jsonb
--
INSERT INTO jsonbs (j) VALUES
    ('{"aaa":[1,2.00,3]}'),
    ('{"ggg":"aaa"}'),
    ('["bbb", "aaa"]');
CREATE INDEX ON jsonbs USING gin (j); -- test ambuild
INSERT INTO jsonbs (j) VALUES
    ('"aaa"'),
    ('3.0'),
    ('{"aaa":{"bbb":[2,4], "ccc":{"ddd":6}}, "eee":8}'); -- test aminsert
-- test amgetbitmap
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ? 'aaa';
SELECT * FROM jsonbs WHERE j ? 'aaa';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}';
SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}';
SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"bbb":[4]}';
SELECT * FROM jsonbs WHERE j @> '{"bbb":[4]}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}';
SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"';
SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"';
EXPLAIN (costs off) SELECT count(*) FROM jsonbs WHERE j ? 'aaa';
SELECT count(*) FROM jsonbs WHERE j ? 'aaa';

--
-- jsonb_path
--
DROP INDEX jsonbs_j_idx;
CREATE INDEX ON jsonbs USING gin (j jsonb_path_ops); -- test ambuild
INSERT INTO jsonbs (j) VALUES
    ('{"aaa":{"bbb":[2], "ccc":{"ddd":6}}, "eee":[]}'); -- test aminsert
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ? 'aaa';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?| '{"ggg", "eee"}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j ?& '{"aaa", "eee"}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}';
SELECT * FROM jsonbs WHERE j @> '{"aaa":{"bbb":[4]}}';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)';
SELECT * FROM jsonbs WHERE j @? '$.aaa[*] ? (@ == 2)';
EXPLAIN (costs off) SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"';
SELECT * FROM jsonbs WHERE j @@ '$.ggg starts with "aa"';

--
-- Multicolumn
--
INSERT INTO multi VALUES
    (to_tsvector('simple', 'a b'), ARRAY['c', 'd'], ARRAY[5.0, 6.1], '{"f":3}',
     '{"r":[3,6.5]}'),
    (to_tsvector('simple', '0'), ARRAY['0'], ARRAY[0], '0', '0');
CREATE INDEX ON multi USING gin (v, a1); -- test ambuild
CREATE INDEX ON multi USING gin (a2, a2); -- test ambuild
CREATE INDEX ON multi USING gin (j1 jsonb_ops, j2 jsonb_path_ops); -- test ambuild
INSERT INTO multi VALUES
    (to_tsvector('simple', 'c d'), ARRAY['a'], ARRAY[5, 6.1], '{"f":5}',
     '{"r":[3.0,6.5]}'); -- test aminsert
EXPLAIN (costs off) SELECT count(*) FROM multi WHERE v @@ 'a' or a1 && ARRAY['a'];
SELECT count(*) FROM multi WHERE v @@ 'a' or a1 && ARRAY['a'];
EXPLAIN (costs off) SELECT count(*) FROM multi WHERE a2 && ARRAY[5.0]::float[];
SELECT count(*) FROM multi WHERE a2 && ARRAY[5.0]::float[];
EXPLAIN (costs off) SELECT count(*) FROM multi WHERE j1 ? 'f' and j2 @> '{"r":[3.0]}';
SELECT count(*) FROM multi WHERE j1 ? 'f' and j2 @> '{"r":[3.0]}';

--
-- Expression index
--
INSERT INTO expression VALUES
    (to_tsvector('simple', 'a b c'), ARRAY['d', 'e', 'f'], '{"g":["h","i"]}');
CREATE INDEX ON expression USING gin (tsvector_to_array(v)); -- test ambuild
CREATE INDEX ON expression USING gin (array_to_tsvector(a)); -- test ambuild
CREATE INDEX ON expression USING gin (
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
CREATE INDEX ON partial USING gin (v) WHERE v @@ 'c'; -- test ambuild
CREATE INDEX ON partial USING gin (a) WHERE j @> '{"g":["i"]}'; -- test ambuild
CREATE INDEX ON partial USING gin (j) WHERE a && ARRAY['f']; -- test ambuild
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

-- Cleanup
DISCARD TEMP;

--
-- Try creating gin index on Yugabyte table.
--
CREATE TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE INDEX ON vectors USING gin (v);

-- Cleanup
DROP TABLE vectors;
RESET enable_seqscan;
RESET enable_bitmapscan;
