SET search_path TO documentdb_core, documentdb_api_catalog, documentdb_api, documentdb_api_internal, public;
SET citus.next_shard_id TO 91000000;
SET documentdb.next_collection_id TO 91000;
SET documentdb.next_collection_index_id TO 91000;


SELECT documentdb_api.create_collection('db', 'create_indexes_vector');

-- now you get different errors
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": "cosmosSearch"}, "name": "foo_1"  } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": 1 }, "name": "foo_1", "cosmosSearchOptions": { } } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 1 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS" } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "dimensions": 10 } } ] }', true);

-- create a valid indexes
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "a": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "b": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 200, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_vector", "indexes": [ { "key": { "c": "cosmosSearch" }, "name": "foo_3", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 300, "similarity": "L2", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

\d documentdb_data.documents_91000

\d documentdb_api_catalog.index_spec_type_internal

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "create_indexes_vector" }') ORDER BY 1;

SELECT documentdb_api.insert_one('db', 'create_indexes_vector', '{ "a": "some sentence", "elem": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'create_indexes_vector', '{ "a": "some other sentence", "elem": [8.0, 5.0, 0.1 ] }');

SELECT document -> 'a' FROM documentdb_api.collection('db', 'create_indexes_vector') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[10, 1, 2]';

SELECT document -> 'a' FROM documentdb_api.collection('db', 'create_indexes_vector') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[10, 1, 2]';

SELECT document -> 'a' FROM documentdb_api.collection('db', 'create_indexes_vector') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[3, 5, 2]' limit 1;

SELECT document -> 'a' FROM documentdb_api.collection('db', 'create_indexes_vector') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[3, 5, 2]' DESC limit 1;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'create_indexes_vector');

\d documentdb_data.documents_91000;

begin;
set local enable_seqscan to off;
EXPLAIN(COSTS OFF) SELECT document -> 'a' FROM documentdb_api.collection('db', 'create_indexes_vector')
    ORDER BY (documentdb_api_internal.bson_extract_vector(document, 'elem'::text)::vector(3)) <=> '[10, 1, 2]';
ROLLBACK;

-- Create an index
-- Also create a geospatial index first so that we can test these are ignore for the sort clause and only vector index is considered
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector": "2dsphere" }, "name": "foo_1_2ds" } ] }', true);
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence1", "myvector": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence2", "myvector": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector": [8.0, 1.0, 9.0 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector": [8.0, 1.0, 8.0 ] }');


-- bad queries 
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [ 8.0, 1.0 ], "limit": 1, "path": "myvector", "numCandidates": 10 } }]}');

-- correct queries
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9.0], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9.0], "limit": 2, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 9, 1], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');

-- Drop the geospatial index
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "vectorIndexColl", "index": "foo_1_2ds"}');

-- direct query that does not require rewrite
SELECT document -> 'myvector' FROM documentdb_api.collection('db', 'vectorIndexColl') ORDER BY 
documentdb_api_internal.bson_extract_vector(document, 'myvector') <=> documentdb_api_internal.bson_extract_vector('{ "path" : "myvector", "vector": [8.0, 1.0, 9.0], "k": 10 }', 'vector') LIMIT 1;

-- same index with different dimensions
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "vectorIndexColl", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 4 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);

-- second index on a differet path
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector2": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 4 } } ] }', true);

SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence2", "myvector": [3.0, 5.0], "myvector2": [3.0, 5.0, 1.1, 4 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence2", "myvector": [3.0, 5.0, 1], "myvector2": [3.0, 5.0, 1.1] }');

SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence1", "myvector": [3.0, 5.0, 1.1 ], "myvector2": [3.0, 5.0, 1.1, 4 ]  }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector": [8.0, 1.0, 9.0 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector2": [8.0, 1.0, 8.0, 8 ] }');

-- query should fail
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9.0, 2], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9], "limit": 1, "path": "myvector2", "numCandidates": 10 } }, { "$project": { "myvector2": 1, "_id": 0 }} ]}');

-- query should work
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9, -1], "limit": 1, "path": "myvector2", "numCandidates": 10 } }, { "$project": { "myvector2": 1, "_id": 0 }} ]}');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','vectorIndexColl');
begin;
set local enable_seqscan to off;
EXPLAIN(COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9, -1], "limit": 1, "path": "myvector2", "numCandidates": 10 } }, { "$project": { "myvector2": 1, "_id": 0 }} ]}');
ROLLBACK;

-- check other two vector indexes
SET search_path TO documentdb_api_catalog, documentdb_api, public;
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector3": "cosmosSearch" }, "name": "foo_3_ip", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexColl", "indexes": [ { "key": { "myvector4": "cosmosSearch" }, "name": "foo_4_l2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "L2", "dimensions": 4 } } ] }', true);
RESET client_min_messages;

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "vectorIndexColl" }') ORDER BY 1;

\d documentdb_data.documents_91001;

SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector3": [8.0, 1.0, 9.0 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexColl', '{ "elem": "some sentence3", "myvector4": [8.0, 1.0, 8.0, 8 ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9.0], "limit": 1, "path": "myvector3", "numCandidates": 10 } }, { "$project": { "myvector3": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexColl", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 8.0, 7], "limit": 1, "path": "myvector4", "numCandidates": 10 } }, { "$project": { "myvector4": 1, "_id": 0 }} ]}');

-- Query on a non-existent collection
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorCollNonExistent", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 8.0, 7], "limit": 1, "path": "myvector4", "numCandidates": 10 } }, { "$project": { "myvector4": 1, "_id": 0 }} ]}');


-- Test vector similarity score projection [COS]
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "similarity_score_cos", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'similarity_score_cos', '{ "_id": 1, "a": "some sentence", "b": 10, "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_cos', '{ "_id": 2, "a": "some other sentence", "b": 20, "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_cos', '{ "_id": 3, "a": "some sentence", "b": 30, "v": [3, 2, 1 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_cos', '{ "_id": 4, "a": "some other sentence", "b": 40, "v": [-3, -2.0, -1 ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$project": { "rank": { "$meta": "searchScore" } } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "v": 0 }}, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');


SELECT documentdb_distributed_test_helpers.drop_primary_key('db','similarity_score_cos');

begin;
set local enable_seqscan to off;
EXPLAIN (costs off, verbose on) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 1, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_cos", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
ROLLBACK;

-- Test vector similarity score projection [L2]
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "similarity_score_l2", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "sim_l2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "L2", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'similarity_score_l2', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_l2', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_l2', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_l2', '{ "_id": 4, "a": "some other sentence", "v": [-3, -2.0, -1 ] }');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','similarity_score_l2');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_l2", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_l2", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');

begin;
set local enable_seqscan to off;
EXPLAIN (costs off, verbose on) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_l2", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 1, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_l2", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
ROLLBACK;

-- Test vector similarity score projection [IP]
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "similarity_score_ip", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "sim_ip", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "IP", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'similarity_score_ip', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_ip', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_ip', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');
SELECT documentdb_api.insert_one('db', 'similarity_score_ip', '{ "_id": 4, "a": "some other sentence", "v": [-3, -2.0, -1 ] }');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','similarity_score_ip');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_ip", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_ip", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');

begin;
set local enable_seqscan to off;
EXPLAIN (costs off, verbose on) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_ip", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ -1.0, 2, -3.0 ], "k": 1, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "similarity_score_ip", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
ROLLBACK;

-- ivf, Create index first and shard later 
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_ivf_index_first_shard_later", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

ANALYZE;

BEGIN;
SET local client_min_messages TO WARNING;
SELECT documentdb_api.shard_collection('db','create_ivf_index_first_shard_later', '{"a":"hashed"}', false);
END;

SELECT documentdb_api.insert_one('db', 'create_ivf_index_first_shard_later', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'create_ivf_index_first_shard_later', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'create_ivf_index_first_shard_later', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');

ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_ivf_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
-- nProbes = 1, will return 1 result, the vector [3, 2, 1] is only on one shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_ivf_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "nProbes": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- nProbes = 1, will return 2 results, the vector [1, 2, 3] is on both shards
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_ivf_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 1, 2, 3 ], "k": 4, "path": "v", "nProbes": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- nProbes = 100, will return all data
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_ivf_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "nProbes": 100 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- ivf, Shard collection first and create index later
SELECT documentdb_api.shard_collection('db','shard_first_create_ivf_index_later', '{"a":"hashed"}', false);

SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "shard_first_create_ivf_index_later", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'shard_first_create_ivf_index_later', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'shard_first_create_ivf_index_later', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'shard_first_create_ivf_index_later', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');

ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_ivf_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
-- nProbes = 1, will return 1 result, the vector [3, 2, 1] is only on one shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_ivf_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "nProbes": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- nProbes = 1, will return 2 results, the vector [1, 2, 3] is on both shards
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_ivf_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 1, 2, 3 ], "k": 4, "path": "v", "nProbes": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- nProbes = 100, will return all data
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_ivf_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "nProbes": 100 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- hnsw, Create index first and shard later 
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_hnsw_index_first_shard_later", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);

ANALYZE;

BEGIN;
SET local client_min_messages TO WARNING;
SELECT documentdb_api.shard_collection('db','create_hnsw_index_first_shard_later', '{"a":"hashed"}', false);
END;

SELECT documentdb_api.insert_one('db', 'create_hnsw_index_first_shard_later', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'create_hnsw_index_first_shard_later', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'create_hnsw_index_first_shard_later', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');

ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_hnsw_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
-- efSearch = 1, search [3, 2, 1] will return 2 results, one vector from each shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_hnsw_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "efSearch": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- efSearch = 1, search [1, 2, 3] will return 2 results, one vector from each shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_hnsw_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 1, 2, 3 ], "k": 4, "path": "v", "efSearch": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- efSearch = 10, will return all data
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "create_hnsw_index_first_shard_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "efSearch": 10 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- hnsw, Shard collection first and create index later
SELECT documentdb_api.shard_collection('db','shard_first_create_hnsw_index_later', '{"a":"hashed"}', false);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "shard_first_create_hnsw_index_later", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);

SELECT documentdb_api.insert_one('db', 'shard_first_create_hnsw_index_later', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'shard_first_create_hnsw_index_later', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT documentdb_api.insert_one('db', 'shard_first_create_hnsw_index_later', '{ "_id": 3, "a": "some sentence", "v": [3, 2, 1 ] }');

ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_hnsw_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v" }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
-- efSearch = 1, search [3, 2, 1] will return 2 results, one vector from each shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_hnsw_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "efSearch": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- efSearch = 1, search [1, 2, 3] will return 2 results, one vector from each shard
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_hnsw_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 1, 2, 3 ], "k": 4, "path": "v", "efSearch": 1 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- efSearch = 10, will return all data
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "shard_first_create_hnsw_index_later", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3, 2, 1 ], "k": 4, "path": "v", "efSearch": 10 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

-- multiple cosmosSearch indexes on a same path
-- create a hnsw index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
-- same name same options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
-- same name different options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 8, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
-- different name same options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
-- different name different options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
-- different name different kind ivf
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_3", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);

-- create a ivf index
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "multiple_indexes", "index": "foo_1"}');
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
RESET client_min_messages;

-- same name same options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
-- same name different options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 200, "similarity": "COS", "dimensions": 3 } } ] }', true);
-- different name same options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
-- different name different options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "IP", "dimensions": 3 } } ] }', true);
-- different name different kind hnsw
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "multiple_indexes", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_4", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "multiple_indexes", "index": "foo_1"}');