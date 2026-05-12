SET search_path TO documentdb_api_catalog, documentdb_core, documentdb_api, public;
SET citus.next_shard_id TO 6750000;
SET documentdb.next_collection_id TO 67500;
SET documentdb.next_collection_index_id TO 67500;

-- Delete all other indexes from previous tests to reduce flakiness
WITH deleted AS (
  DELETE FROM documentdb_api_catalog.collection_indexes
  WHERE collection_id != 67500
  RETURNING 1
) SELECT true FROM deleted UNION ALL SELECT true LIMIT 1;


-- Reset the counters by making a call to the counter and discarding the results
select count(*)*0 as count from documentdb_api_internal.command_feature_counter_stats(true);

-- vector index creation error
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "a": "cosmosSearch"}, "name": "foo_1"  } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "a": 1 }, "name": "foo_1", "cosmosSearchOptions": { } } ] }', true);

-- create collection
SELECT documentdb_api.create_collection_view('db', '{ "create": "feature_counter_col" }');

-- create view
SELECT documentdb_api.create_collection_view('db', '{ "create": "feature_counter_col_view", "viewOn": "feature_counter_col" }');

-- now collMod it
SELECT documentdb_api.coll_mod('db', 'feature_counter_col_view', '{ "collMod": "feature_counter_col_view", "viewOn": "feature_counter_col", "pipeline": [ { "$limit": 10 } ] }');

-- create a valid indexes
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "a": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "b": "cosmosSearch" }, "name": "foo_2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 200, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "c": "cosmosSearch" }, "name": "foo_3", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 300, "similarity": "L2", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "f": "text" }, "name": "a_text" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "uniq": 1 }, "name": "uniq_1", "unique": true } ] }', true);
RESET client_min_messages;

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

SELECT document -> 'a' FROM documentdb_api.collection('db', 'feature_counter_col') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[10, 1, 2]';
SELECT document -> 'a' FROM documentdb_api.collection('db', 'feature_counter_col') ORDER BY documentdb_api_internal.bson_extract_vector(document, 'elem') <=> '[10, 1, 2]';

-- bad queries 
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0], "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col", "pipeline": [{ "$vectorSearch": { "limit": 1, "path": "myvector", "numCandidates": 10 } }, { "$project": { "myvector": 1, "_id": 0 }} ]}');

-- Use unwind, lookup 
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } } ], "cursor": {} }');
-- add $unset
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$unset": "_id" }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');
-- add skip + limit
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }, { "$skip": 1 }], "cursor": {} }');

-- match + project + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1, "c": "$_id", "_id": 0 } }, { "$match": { "c": { "$gt": "2" } } }], "cursor": {} }');
-- replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceRoot": { "newRoot": "$e" } } ], "cursor": {} }');
-- replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceWith": "$e" } ], "cursor": {} }');
-- sort + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "_id": { "$gt": "1" } } } ], "cursor": {} }');
-- match + sort
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$sort": { "_id": 1 } } ], "cursor": {} }');
-- sortByCount
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$sortByCount": { "$eq": [ { "$mod": [ { "$toInt": "$_id" }, 2 ] }, 0  ] } }, { "$sort": { "_id": 1 } }], "cursor": {} }');
-- $group
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$max": "$_id" }, "e": { "$count": 1 } } }], "cursor": {} }');
-- $group with first/last
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$first": "$_id" }, "e": { "$last":  "$_id" } } }], "cursor": {} }');
-- $group with firstN/lastN
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$firstN": { "input":"$_id", "n":5 } }, "e": { "$lastN": { "input":"$_id", "n":5 } } } }], "cursor": {} }');
-- $group with firstN/lastN w N>10
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col2", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$firstN": { "input":"$_id", "n":15 } }, "e": { "$lastN": { "input":"$_id", "n":15 } } } }], "cursor": {} }');
-- collation
SET documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "feature_counter_col2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "feature_counter_col2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 10, "collation": { "locale": "fr_CA", "strength" : 3 } }');
RESET documentdb_core.enablecollation;


-- Create TTL index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "feature_counter_col2", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 5}]}', true);

-- Run validate command
SELECT documentdb_api.validate('db', '{ "validate" : "validatecoll", "repair" : true }' );

-- Print without resetting the counters
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(false);

-- print and reset the counters
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- check other two vector indexes
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "myvector3": "cosmosSearch" }, "name": "foo_3_ip", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "myvector4": "cosmosSearch" }, "name": "foo_4_l2", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "L2", "dimensions": 4 } } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'vectorIndexCollFC', '{ "elem": "some sentence3", "myvector3": [8.0, 1.0, 9.0 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexCollFC', '{ "elem": "some sentence3", "myvector4": [8.0, 1.0, 8.0, 8 ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 9.0], "limit": 1, "path": "myvector3", "numCandidates": 10 } }, { "$project": { "myvector3": 1, "_id": 0 }} ]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 8.0, 7], "limit": 1, "path": "myvector4", "numCandidates": 10 } }, { "$project": { "myvector4": 1, "_id": 0 }} ]}');

-- Query on a non-existent collection
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorCollNonExistent", "pipeline": [{ "$vectorSearch": { "queryVector": [8.0, 1.0, 8.0, 7], "limit": 1, "path": "myvector4", "numCandidates": 10 } }, { "$project": { "myvector4": 1, "_id": 0 }} ]}');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- check vector indexes
SET client_min_messages TO WARNING;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "vector_ivf": "cosmosSearch" }, "name": "ivf_index", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "L2", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "vector_hnsw": "cosmosSearch" }, "name": "hnsw_index", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 4 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "vector_ivf_half": "cosmosSearch" }, "name": "ivf_index_half", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "L2", "dimensions": 3, "compression": "half" } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "vector_hnsw_half": "cosmosSearch" }, "name": "hnsw_index_half", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 4, "compression": "half" } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "vectorIndexCollFC", "indexes": [ { "key": { "elem": 1 }, "name": "elem_index" } ] }', true);
RESET client_min_messages;

SELECT documentdb_api.insert_one('db', 'vectorIndexCollFC', '{ "_id": 1, "elem": "some sentence ivf", "vector_ivf": [8.0, 1.0, 9.0 ] }');
SELECT documentdb_api.insert_one('db', 'vectorIndexCollFC', '{ "_id": 2, "elem": "some sentence hnsw", "vector_hnsw": [8.0, 1.0, 8.0, 8 ] }');
ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "vector_ivf", "nProbes": 10}  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0, 1.0 ], "k": 2, "path": "vector_hnsw", "efSearch": 5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "vector_ivf_half" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0, 1.0 ], "k": 2, "path": "vector_hnsw_half" }  } } ], "cursor": {} }');

BEGIN;
SET LOCAL documentdb.enableVectorPreFilter = on;
SET local enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "vector_ivf", "nProbes": 10, "filter": { "elem": { "$gt": "some p" } }  }}} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL documentdb.enableVectorPreFilter = on;
SET local enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "vectorIndexCollFC", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0, 1.0 ], "k": 2, "path": "vector_hnsw", "efSearch": 5, "filter": { "elem": { "$gt": "some p" } }  }}} ], "cursor": {} }');
ROLLBACK;

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- aggregation operators counters
SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{"a": 1}');
SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{"a": 2}');
SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{"a": 1}');

-- should only count once per query, not once per document
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"_id": 0, "result": { "$add": ["$a", 1]}}}], "cursor": {} }');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(false);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"_id": 0, "result": { "$add": ["$a", 1]}}}], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"_id": 0, "result": { "$multiply": ["$a", 1]}}}], "cursor": {} }');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- nested should be counted
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"_id": 0, "result": { "$filter": {"input": [1, 2, 3, 4], "cond": {"$eq": ["$$this", 3]}}}}}], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"_id": 0, "result": { "$filter": {"input": [1, 2, 3, 4], "cond": {"$gt": ["$$this", 3]}}}}}], "cursor": {} }');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- should not count for non-existent operators
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_counter_col3", "pipeline": [ {"$project": {"result": { "$nonExistent": {"input": [1, 2, 3, 4], "cond": {"$eq": ["$$this", 3]}}}}}], "cursor": {} }');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- Test feature counters for geospatial
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "2dkey": "2d"}, "name": "my_2d_idx"  } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col", "indexes": [ { "key": { "2dspherekey": "2dsphere"}, "name": "my_2dsphere_idx"  } ] }', true);

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{"2dkey": [1, 1]}');
SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{"2dspherekey": [1, 1]}');

SELECT document -> '2dkey' FROM documentdb_api.collection('db', 'feature_counter_col3') WHERE document @@ '{"2dkey": {"$geoWithin": {"$box": [[0, 0], [1, 1]]}}}';
SELECT document -> '2dkey' FROM documentdb_api.collection('db', 'feature_counter_col3') WHERE document @@ '{"2dkey": {"$within": {"$box": [[0, 0], [1, 1]]}}}';
SELECT document -> '2dspherekey' FROM documentdb_api.collection('db', 'feature_counter_col3') WHERE document @@ '{"2dspherekey": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 1], [1, 1], [1, 0], [0,0]]] } }}}';
SELECT document -> '2dspherekey' FROM documentdb_api.collection('db', 'feature_counter_col3') WHERE document @@ '{"2dspherekey": {"$geoIntersects": {"$geometry": { "type": "Point", "coordinates": [1, 1] } }}}';

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- Test feature counter for $text
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "feature_counter_col3", "indexes": [ { "key": { "textkey": "text" }, "name": "my_txt_idx" } ] }', true);

SELECT documentdb_api.insert_one('db', 'feature_counter_col3', '{ "textkey": "this is a cat" }');

SELECT document -> 'textkey' FROM documentdb_api.collection('db', 'feature_counter_col3') WHERE document @@ '{ "$text": { "$search": "cat" } }';

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- TTL index usage tests

SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 0, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 1, "ttl" : { "$date": { "$numberLong": "0" } } }', NULL);
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 2, "ttl" : { "$date": { "$numberLong": "100" } } }', NULL);
    -- Documents with date older than when the test was written
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 3, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
    -- Documents with date way in future
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 4, "ttl" : { "$date": { "$numberLong": "2657899731608" } } }', NULL);
    -- Documents with date array
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 5, "ttl" : [{ "$date": { "$numberLong": "100" }}] }', NULL);
    -- Documents with date array, should be deleted based on min timestamp
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 6, "ttl" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 7, "ttl" : [true, { "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 8, "ttl" : true }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('db','feature_usage_ttlcoll', '{ "_id" : 9, "ttl" : "would not expire" }', NULL);

-- 1. Create TTL Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "feature_usage_ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 5}]}', true);

-- 2. List All indexes --
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "feature_usage_ttlcoll" }') ORDER BY 1;
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'feature_usage_ttlcoll') ORDER BY collection_id, index_id;

-- 4. Call ttl purge procedure with a batch size of 2
SET documentdb.repeatPurgeIndexesForTTLTask to off;
CALL documentdb_api_internal.delete_expired_rows(3);
CALL documentdb_api_internal.delete_expired_rows(3);
CALL documentdb_api_internal.delete_expired_rows(3);

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);
SET documentdb.TTLSlowBatchDeleteThresholdInMS to 0; /* 0 makes all batches slow*/
CALL documentdb_api_internal.delete_expired_rows(300);
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true); /* 8 shards each for 2 indexes*/
RESET documentdb.TTLSlowBatchDeleteThresholdInMS;

-- Feature counter for _internalInhibitOptimization
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_usage_inhibit", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$_internalInhibitOptimization": 1 }, { "$replaceWith": "$e" } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_usage_inhibit", "pipeline": [ { "$sort": { "_id": 1 } }, { "$_internalInhibitOptimization": 1 }, { "$match": { "_id": { "$gt": "1" } } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "feature_usage_inhibit", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$_internalInhibitOptimization": 1 }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');

SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

SELECT documentdb_api.insert('db', '{"insert":"writeFC", "documents":[
   { "_id" : 1, "movie": "Iron Man 3", "Budget": 180000000, "year": 2011 }
]}');

SELECT documentdb_api.insert('db', '{"insert":"writeFC", "documents":[
   { "_id" : 2, "movie": "Wolverine", "Budget": 180000000, "year": 2012 },
   { "_id" : 3, "movie": "Spider Man", "Budget": 180000000, "year": 2013 },
   { "_id": 4, "movie": "AntMan", "Budget": 180000000, "year": 2015, "actors": ["Paul Rudd", "Evangeline Lilly"], "tags": ["Marvel", "Superhero"], "ratings": {"critics": 80, "audience": 90}}
]}');

SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 1},"u":{"$set":{"year": "1998" }},"multi":true}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 1},"u":{"$set":{"year": "2001" }},"multi":true}, {"q": {"_id": 2},"u":{"$set":{"year": "2002" }},"multi":true}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$inc":{"year": 1 }},"multi":false}]}');
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$min":{"year": 3000 }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$max":{"year": 2000 }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$push":{"actors": "New Actor" }},"multi":false}]}');
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$pop": { "actors": -1 }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$rename": { "year": "Year" }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$setOnInsert": { "Year": 2015 }},"multi":false}]}');
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$addToSet": { "actors": "Paul Rudd" }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$pullAll": { "actors": ["No Actor"] }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$pull": { "actors": "No Actor" }},"multi":false}]}');
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$mul": { "Budget": 1 }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$currentDate": { "updatedAt": true }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$bit": { "Year": { "and": 2016 } }},"multi":false}]}');
SELECT documentdb_api.update('db', '{"update": "writeFC", "updates":[{"q": {"_id": 4},"u":{"$unset": { "tags": "" }},"multi":false}]}');
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- Test: Feature counter for list_databases command

-- Reset feature counters
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

-- execute list_databases command but suppress all output with LIMIT 0 to avoid varying result
SELECT * FROM documentdb_api.list_databases('{"listDatabases": 1, "nameOnly":true}') LIMIT 0;
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(false);

SELECT * FROM documentdb_api.list_databases('{"listDatabases": 1}') LIMIT 0;
SELECT documentdb_distributed_test_helpers.get_feature_counter_pretty(true);

