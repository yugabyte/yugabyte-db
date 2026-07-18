SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_api_internal,documentdb_core;

SET citus.next_shard_id TO 695000;
SET documentdb.next_collection_id TO 69500;
SET documentdb.next_collection_index_id TO 69500;

set documentdb.enableExtendedExplainPlans to on;

SELECT documentdb_api.drop_collection('hint_db', 'query_index_hints') IS NOT NULL;
SELECT documentdb_api.create_collection('hint_db', 'query_index_hints');

-- create various kinds of indexes.
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1 }, "name": "a_1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "b": 1, "c": 1 }, "name": "b_1_c_1" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "d": "hashed" }, "name": "d_hashed" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "$**": "text" }, "name": "e_text" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "f": 1 }, "name": "f_1", "sparse": true }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "g.$**": 1 }, "name": "g_1" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "h": 1 }, "name": "h_1", "sparse": true }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "i": 1, "j": 1 }, "name": "i_1_j_1", "unique": true }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "k": 1, "l": 1 }, "name": "k_1_l_1", "sparse": true, "unique": true }] }', true);

\d documentdb_data.documents_69501

-- now insert some sample docs
SELECT documentdb_api.insert_one('hint_db', 'query_index_hints', '{ "_id": 1, "a": 1, "c": 1 }');

-- query index hints by name - every index works except text index
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "a_1" }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "b_1_c_1" }');

-- this pushes to a seqscan because the index is hashed
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "d_hashed" }');

-- this should fail.
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "e_text" }');

-- pushes as an exists true query since the index is sparse
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "f_1" }');

-- cannot push to wildcard index since it is a wildcard index
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "g_1" }');

EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "i_1_j_1" }');

-- pushes as an exists true query since the index is sparse
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "h_1" }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "k_1_l_1" }');

-- query index hint by key - works the same as name
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "a": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "b": 1, "c": 1 } }');

-- this pushes to a seqscan because the index is hashed
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "d": "hashed" } }');

-- this should fail.
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "$**": "text" } }');

-- pushes as an exists true query since the index is sparse
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "f": 1 } }');

-- cannot push to wildcard index since it is a wildcard index
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "g.$**": 1 } }');

EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "i": 1, "j": 1 } }');

-- pushes as an exists true query since the index is sparse
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "h": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "k": 1, "l": 1 } }');

-- hints when no index exists
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "nonexistent" }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "nonexistent": 1 } }');

-- natural hint picks _id index.
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "$natural": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "$natural": -1 } }');

-- more error cases
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1 }, "sparse": true, "name": "a_2" }] }', true);

-- fails due to multiple indexes matching.
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": { "a": 1 } }');

EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "a_2" }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "a_1" }');


-- try with composite indexes.
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1 }, "enableCompositeTerm": true, "name": "a_3" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "k": 1, "l": 1 }, "enableCompositeTerm": true, "name": "k_1_l_1-2" }] }', true);

EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "a_3" }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { "x": 1 }, "hint": "k_1_l_1-2" }');

-- try natural and id hint with no filters on sharded collections.
SELECT documentdb_api.shard_collection('hint_db', 'query_index_hints', '{"_id": "hashed"}', FALSE);

EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { }, "hint": { "$natural": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "filter": { }, "hint": { "_id": 1 } }');
