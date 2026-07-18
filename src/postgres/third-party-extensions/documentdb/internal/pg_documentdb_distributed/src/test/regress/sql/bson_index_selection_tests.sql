SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 50000;
SET documentdb.next_collection_id TO 5000;
SET documentdb.next_collection_index_id TO 5000;


SELECT documentdb_api.create_collection('db', 'indexselection');

-- create a wildcard index on path a.b.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indexselection', 'indexsel_path_a_b', '{"a.b.$**": 1}'), true);

-- create a non wildcard index on path 'b.c'
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indexselection', 'indexsel_path_b_c', '{"b.c": 1}'), true);

-- create a wildcard projection index with include paths at specific trees 'd.e.f' and 'g.h'
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexselection", "indexes": [ { "key": { "$**": 1 }, "name": "indexsel_path_wild_def_gh", "wildcardProjection": { "d.e.f": 1, "g.h": 1 } }]}', true);

-- create two overlapping indexes.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indexselection', 'indexsel_path_overl_2', '{"r.s.$**": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indexselection', 'indexsel_path_overl_1', '{"r.s.t": 1}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indexselection', 'some_long_index_name_that_is_definitely_over_64_characters_to_test_explain_index_name_length', '{"randomPath": 1}'), true);

-- create a wildcard projection index that excludes all the above paths.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexselection", "indexes": [ { "key": { "$**": 1 }, "name": "indexsel_path_wild_excl_1", "wildcardProjection": { "d": 0, "g": 0, "a": 0, "b": 0, "r": 0, "randomPath": 0 } }]}', true);

\d documentdb_data.documents_5000

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;

-- now explain queries

-- filter on a.b - should select indexsel_path_a_b
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "a.b": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "a.b.c.d": { "$gte" : 1 }}';

-- does not match a_b (sequential scan)
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "a.bar": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "a": { "$gte" : 1 }}';

-- path root at is not exactly 'a' - matches excl_1.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "abc": { "$gte" : 1 }}';

-- filter on b.c - should select indexsel_path_b_c
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "b.c": { "$gte" : 1 }}';

-- not a wildcard - does not match.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "b.car": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "b.c.d": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "b": { "$gte" : 1 }}';

-- wildcard matches a projection
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "d.e.f.g": { "$gte" : 1 }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "g.1": { "$gte" : 1 }}';

-- any other root paths or nested paths match the wildcard projection index.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "e": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "k.l.m": { "$gte" : 1 }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "graph": { "$gte" : 1 }}';

-- on the overlapping indexes, last path wins.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "r.s.t": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "r.s.u.1": { "$gte" : 1 }}';

-- intersect two indexes
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "$and": [ { "a.b.c.d": { "$gte" : 1 } }, { "b.c": 2 } ]}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "$or": [ { "a.b.c.d": { "$gte" : 1 } }, { "b.c": 2 } ]}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "randomPath": 1 }';

-- now insert documents that match each of the filters
-- matches a.b
SELECT documentdb_api.insert_one('db','indexselection', '{"_id": 1, "a" : { "b" : { "c": 1 } } }', NULL);

-- matches b.c
SELECT documentdb_api.insert_one('db','indexselection', '{"_id": 2, "b" : { "c" : 0 }}', NULL);

-- matches g.h
SELECT documentdb_api.insert_one('db','indexselection', '{"_id": 3, "g" : { "h" :  { "i" : 0 } } }', NULL);

-- matches wildcard
SELECT documentdb_api.insert_one('db','indexselection', '{"_id": 4, "k" : { "l" :  { "m" : 0 } } }', NULL);

-- matches none.
SELECT documentdb_api.insert_one('db','indexselection', '{"_id": 5, "g" : 2 }', NULL);

-- now query each one to see documents being returned.
SELECT COUNT(*) FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "a.b.c" : 1 }';
SELECT COUNT(*) FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "b.c" : { "$eq": 0 } }';
SELECT COUNT(*) FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "g.h.i" : { "$gt": -1 } }';
SELECT COUNT(*) FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "k.l.m" : { "$lt": 1 } }';
SELECT COUNT(*) FROM documentdb_api.collection('db', 'indexselection') WHERE document @@ '{ "g" : { "$exists": 1 } }';

ROLLBACK;
