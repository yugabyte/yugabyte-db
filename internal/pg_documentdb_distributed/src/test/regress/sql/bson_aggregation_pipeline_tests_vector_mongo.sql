SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 8300000;
SET documentdb.next_collection_id TO 8300;
SET documentdb.next_collection_index_id TO 8300;

SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector', '{ "_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector', '{ "_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector', '{ "_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector', '{ "_id": 6,  "a": "some sentence", "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector', '{ "_id": 7,  "a": "some other sentence", "v": [8.0, 5.0, 0.1 ] }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
ANALYZE;

-- Vector search with vectorSearch
BEGIN;
SET LOCAL enable_seqscan to off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 5.0, 1.1 ], "limit": 1, "path": "v" }  } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 1, "path": "v" }  } ], "cursor": {} }');
ROLLBACK;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 1, "path": "v", "filter": {"a": "some sentence"} }  } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 5.0, 1.1 ], "limit": 1, "path": "v", "filter": {} }  } ], "cursor": {} }');

SELECT documentdb_api.shard_collection('db','aggregation_pipeline_mongo_vector', '{"_id":"hashed"}', false);
ANALYZE;
-- vectorSearch with shard
BEGIN;
SET LOCAL enable_seqscan to off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.0, 1.0 ], "limit": 2, "path": "v", "numCandidates": 100 }  }  ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.0, 1.0 ], "limit": 2, "path": "v", "numCandidates": 100 }  }  ], "cursor": {} }');
ROLLBACK;
----------------------------------------------------------------------------------------------------
-- vectorSearch with filter
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector_filter', '{ "_id": 6, "meta":{ "a": "some sentence", "b": 1 }, "c": true , "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector_filter', '{ "_id": 7, "meta":{ "a": "some other sentence", "b": 2}, "c": true , "v": [8.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector_filter', '{ "_id": 8, "meta":{ "a": "other sentence", "b": 5 }, "c": false, "v": [13.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector_filter', '{ "_id": 9, "meta":{ "a" : [ { "b" : 3 } ] }, "c": false, "v": [15.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_mongo_vector_filter', '{ "_id": 10, "meta":{ "a" : [ { "b" : 5 } ] }, "c": false }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "hnsw_index", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 2, "path": "v", "filter": {"a": "some sentence"} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 1, "path": "v", "filter": {}  } } ], "cursor": {} }');

set documentdb.enableVectorPreFilter = on;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "$**" : 1 }, "name": "wildcardIndex" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "meta.a": 1 }, "name": "idx_meta.a" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "meta.b": 1 }, "name": "numberIndex_meta.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "meta.a.b": 1 }, "name": "idx_meta.a.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "meta": 1 }, "name": "documentIndex_meta" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_mongo_vector_filter", "indexes": [ { "key": { "c": 1 }, "name": "boolIndex_c" } ] }', true);

--------------------------------------------------
-- no match index path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 2, "path": "v", "filter": {"unknownPath": "some sentence"} }  } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 2, "path": "v", "filter": {"meta.c": "some sentence"} } } ], "cursor": {} }');

-- wrong filter type
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 1, "path": "v", "filter": "some sentence" }  } ], "cursor": {} }');

--------------------------------------------------
-- multiple index path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "numCandidates": 100 }  }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "meta.b": { "$eq": 5 } } ] }, { "c": { "$eq": false } } ] } } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "c": { "$eq": false } } ] }, { "meta.b": { "$lt": 5 } } ] } } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.a": { "$regex": "^some", "$options" : "i" } }, { "meta.b": { "$eq": 5 } } ] }, { "meta.b": { "$lt": 5 } } ] } } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "numCandidates": 100 } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "numCandidates": 100 }} ], "cursor": {} }');

SELECT documentdb_api.shard_collection('db','aggregation_pipeline_mongo_vector_filter', '{"_id":"hashed"}', false);
ANALYZE;
-- vectorSearch with filter and shard
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "numCandidates": 100 }  }  ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_mongo_vector_filter", "pipeline": [ { "$vectorSearch": { "queryVector": [ 3.0, 4.9, 1.0 ], "limit": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "numCandidates": 100 }  }  ], "cursor": {} }');

set documentdb.enableVectorPreFilter = off;

--------------------------------------------------
-- Vector search with knnBeta
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_knnbeta', '{ "_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_knnbeta', '{ "_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_knnbeta', '{ "_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_knnbeta', '{ "_id": 6,  "a": "some sentence", "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_knnbeta', '{ "_id": 7,  "a": "some other sentence", "v": [8.0, 5.0, 0.1 ] }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_knnbeta", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);


BEGIN;
SET LOCAL enable_seqscan to off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
ROLLBACK;

set documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": "some" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v", "filter": {} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "score": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "score": 100 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v", "score": {} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_knnbeta", "pipeline": [ { "$search": { "unknowType": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
set documentdb.enableVectorPreFilter = off;
