CREATE TABLE test_remote_filter(a INT, t TEXT, ts TSVECTOR, tq TSQUERY, jb JSONB, js JSON);

INSERT INTO test_remote_filter VALUES
    (1, 'fat', to_tsvector('fat cat'), to_tsquery('cat'), '{"a": 1}', '{"b": 1}'),
    (2, 'bat', to_tsvector('fat cat'), to_tsquery('cat'), '{"a": 2}', '{"b": 2}'),
    (3, 'cat', to_tsvector('fat cat'), to_tsquery('cat'), '{"a": 3}', '{"b": 3}'),
    (4, 'hat', to_tsvector('fat cat'), to_tsquery('cat'), '{"a": 4}', '{"b": 4}');

-- test pushdown of col @@ tsvector
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE tq @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE tq @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE tq @@ to_tsvector(t);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE tq @@ to_tsvector(jb);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE tq @@ to_tsvector(js);

-- test pushdown of const @@ tsvector
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE 'cat'::tsquery @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE 'cat'::tsquery @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE 'cat'::tsquery @@ to_tsvector(t);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE 'cat'::tsquery @@ to_tsvector(jb);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE 'cat'::tsquery @@ to_tsvector(js);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', 'cat') @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', 'cat') @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', 'cat') @@ to_tsvector(t);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', 'cat') @@ to_tsvector(jb);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', 'cat') @@ to_tsvector(js);

-- test pushdown of to_tsquery @@ col
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery(t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE plainto_tsquery(t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE phraseto_tsquery(t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE websearch_to_tsquery(t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE plainto_tsquery('simple', t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE phraseto_tsquery('simple', t) @@ ts;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE websearch_to_tsquery('simple', t) @@ ts;

-- test pushdown of to_tsquery @@ const
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery(t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE plainto_tsquery(t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE phraseto_tsquery(t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE websearch_to_tsquery(t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE to_tsquery('simple', t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE plainto_tsquery('simple', t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE phraseto_tsquery('simple', t) @@ 'cat'::tsvector;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE websearch_to_tsquery('simple', t) @@ 'cat'::tsvector;

-- test pushdown of ts_headline(col, col)
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline(t, tq) = t;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline(jb, tq)->>'a' = '1';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline(js, tq)->>'b' = '1';

-- test pushdown of ts_headline(const, const)
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline('fat', 'cat'::tsquery) = t;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline('{"a": 1}'::jsonb, 'cat'::tsquery)->>'a' = '1';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM test_remote_filter WHERE ts_headline('{"b": 1}'::json, 'cat'::tsquery)->>'b' = '1';

-- test function
CREATE OR REPLACE FUNCTION fn_test_pushdown(TEXT)
RETURNS SETOF test_remote_filter AS $sql$
  SELECT * FROM test_remote_filter WHERE to_tsquery('simple',$1) @@ ts;
$sql$ LANGUAGE SQL;

EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM fn_test_pushdown('cat');
