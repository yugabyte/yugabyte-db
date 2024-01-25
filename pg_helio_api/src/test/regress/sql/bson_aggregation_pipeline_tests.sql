SET search_path TO helio_api,helio_core,helio_api_catalog;

SET helio_api.next_collection_id TO 3500;
SET helio_api.next_collection_index_id TO 3500;


SELECT helio_api.insert_one('db','aggregation_pipeline','{"_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT helio_api.insert_one('db','aggregation_pipeline','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT helio_api.insert_one('db','aggregation_pipeline','{"_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT document FROM helio_api.collection('db', 'aggregation_pipeline') ORDER BY bson_get_value(document, '_id');

-- add newField
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');

-- do 2 addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$set": { "newField2": "someOtherField" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$set": { "newField2": "someOtherField" } } ], "cursor": {} }');

-- add $project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

-- add $unset
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unset": "_id" }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

-- add skip
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }], "cursor": {} }');

-- add limit
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }], "cursor": {} }');

-- add skip + limit
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }, { "$skip": 1 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }, { "$skip": 1 }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }, { "$limit": 1 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }, { "$limit": 1 }], "cursor": {} }');

-- try match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }], "cursor": {} }');

-- match + project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1 } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1 } }], "cursor": {} }');

-- match + project + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1, "c": "$_id", "_id": 0 } }, { "$match": { "c": { "$gt": "2" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1, "c": "$_id", "_id": 0 } }, { "$match": { "c": { "$gt": "2" } } }], "cursor": {} }');

-- unwind
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" } ], "cursor": {} }');

-- match and then unwind
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$unwind": "$a.b" } ], "cursor": {} }');

-- unwind and then match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$match": { "$expr": { "$not": { "$isArray": "$a.b" } } } } ], "cursor": {} }');

-- unwind and addfields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$addFields": { "xyz": "$_id" } } ], "cursor": {} }');

-- replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceRoot": { "newRoot": "$e" } } ], "cursor": {} }');

-- count
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$count": "d" }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$count": "d" }, { "$addFields": { "e": "$d" } }], "cursor": {} }');

-- replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceWith": "$e" } ], "cursor": {} }');

-- sort
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sort": { "_id": 1 } }], "cursor": {} }');

-- sort + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "_id": { "$gt": "1" } } } ], "cursor": {} }');

-- match + sort
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$sort": { "_id": 1 } } ], "cursor": {} }');

-- sortByCount
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sortByCount": { "$eq": [ { "$mod": [ { "$toInt": "$_id" }, 2 ] }, 0  ] } }, { "$sort": { "_id": 1 } }], "cursor": {} }');

-- $group
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$max": "$_id" }, "e": { "$count": 1 } } }], "cursor": {} }');

-- $group with first/last
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$first": "$_id" }, "e": { "$last":  "$_id" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$first": "$_id" }, "e": { "$last":  "$_id" } } }], "cursor": {} }');

-- $group with first/last sorted (TODO)


-- add $sample
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 2 } }, { "$project": { "_id": "1" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 2 } }, { "$project": { "_id": "1" } } ], "cursor": {} }');

-- Sample after pass-through stages
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id": "1" } }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- Sample after sample
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 3 } }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- Sample after other stage
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- internalInhibitOptimization
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$_internalInhibitOptimization": 1 }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$_internalInhibitOptimization": 1 }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');


-- facet
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "c" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } } ], "cursor": {} }');

EXPlAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } } ], "cursor": {} }');

-- facet with parent transform:
SELECT document FROM bson_aggregation_pipeline('db', 
'{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$facet": { "a" : [ { "$unset": "_id" } ],  "b" : [ { "$count": "d" } ], "c": [ { "$replaceWith": { "f": "$_id" } } ] } } ], "cursor": {} }');

-- FIND
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": 2 }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": 2 }');


SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": -2 }');

-- count
SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline" }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": { "_id": { "$gt": "1" } } }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "non_existent_coll" }');

SELECT document FROM helio_api.count_query('db', '{ "count": "aggregation_pipeline", "query": { "_id": { "$gt": "1" } } }');

SELECT document FROM helio_api.count_query('db', '{ "count": "aggregation_pipeline" }');

-- distinct
SELECT document FROM bson_aggregation_distinct('db', '{ "distinct": "aggregation_pipeline", "key": "_id" }');

SELECT document FROM bson_aggregation_distinct('db', '{ "distinct": "non_existent_coll", "key": "foo" }');

SELECT document FROM helio_api.distinct_query('db', '{ "distinct": "aggregation_pipeline", "key": "_id" }');

SELECT document FROM helio_api.distinct_query('db', '{ "distinct": "non_existent_coll", "key": "foo" }');

-- Vector search with cosmosSearch
SELECT helio_api.insert_one('db', 'aggregation_pipeline', '{ "_id": 6, "a": "some sentence", "v": [3.0, 5.0, 1.1 ] }');
SELECT helio_api.insert_one('db', 'aggregation_pipeline', '{ "_id": 7, "a": "some other sentence", "v": [8.0, 5.0, 0.1 ] }');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": "some sentence" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {} }  } } ], "cursor": {} }');

-- search with nProbes
-- numLists <= data size, using data as centroids, to avoid randomized centroids generated by pgvector
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 2 }  } } ], "cursor": {} }');
COMMIT;

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 1 }  } } ], "cursor": {} }');
COMMIT;

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v" }  } } ], "cursor": {} }');
COMMIT;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 10000000 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": -5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": "5" }  } } ], "cursor": {} }');

-- numLists > data size, pgvector will generate randomized centroids, using original vector data to query
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline", "index": "foo_1"}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 10000, "similarity": "COS", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 10000 }  } } ], "cursor": {} }');
COMMIT;

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 5.0, 1.1 ], "k": 2, "path": "v", "nProbes": 1 }  } } ], "cursor": {} }');
COMMIT;

SELECT helio_test_helpers.drop_primary_key('db','aggregation_pipeline');

BEGIN;
set local enable_seqscan to off;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "nProbes": 1 }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
ROLLBACK;

-- Vector search with knnBeta
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": "some" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v", "filter": {} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "score": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "score": 100 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "knnBeta": { "vector": [ 3.0, 5.0, 1.1 ], "k": 1, "path": "v", "score": {} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$search": { "unknowType": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v" }  } } ], "cursor": {} }');

-- $lookup

SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders','{ "_id" : 2, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 3, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 4, "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 5}', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 6, "item" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_orders',' { "_id" : 7, "item" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT helio_api.insert_one('db','agg_pipeline_inventory',' { "_id" : 11, "sku" : "almonds", "description": "product 1", "instock" : 120 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory',' { "_id" : 12, "sku" : "almonds", "description": "product 1", "instock" : 240 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 13, "sku" : "bread", "description": "product 2", "instock" : 80 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 14, "sku" : "cashews", "description": "product 3", "instock" : 60 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 15, "sku" : "pecans", "description": "product 4", "instock" : 70 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 16, "sku" : null, "description": "product 4", "instock" : 70 }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 17, "sku" :  {"a": "x", "b" : 1, "c" : [1, 2, 3]}, "description": "complex object" }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 18, "sku" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "description": "complex array" }', NULL);
SELECT helio_api.insert_one('db','agg_pipeline_inventory','{ "_id" : 19, "sku" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "description": "complex array" }', NULL);


SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } }, { "$sample": { "size": 3 } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "coll_dne", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [], "as": "c" } } ], "cursor": {} }');

BEGIN;
set local citus.enable_local_execution to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

ROLLBACK;

SELECT helio_api.shard_collection('db', 'agg_pipeline_orders', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

BEGIN;
set local citus.enable_local_execution to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

ROLLBACK;

SELECT helio_api.shard_collection('db', 'agg_pipeline_inventory', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "coll_dne", "as": "matched_docs", "localField": "item", "foreignField": "sku", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [], "as": "c" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$match": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$project": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$skip": 1 } ], "cursor": {} }');


-- test sort behavior on sharded/unsharded
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate" : "agg_pipeline_inventory", "pipeline" : [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } }, { "this_predicate_matches_nothing" : true } ] } }, { "$sort" : { "_id" : -1.0 } }, { "$limit" : 1.0 }, { "$project" : { "_id" : 1.0, "b" : { "$round" : "$a" } } } ], "cursor" : {  }, "lsid" : { "id" : { "$binary" : { "base64": "VJmzOaS5R46C4aFkQzrFaQ==", "subType" : "04" } } }, "$db" : "test" }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate" : "aggregation_pipeline", "pipeline" : [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } }, { "this_predicate_matches_nothing" : true } ] } }, { "$sort" : { "_id" : -1.0 } }, { "$limit" : 1.0 }, { "$project" : { "_id" : 1.0, "b" : { "$round" : "$a" } } } ], "cursor" : {  }, "lsid" : { "id" : { "$binary" : { "base64": "VJmzOaS5R46C4aFkQzrFaQ==", "subType" : "04" } } }, "$db" : "test" }');

-- Vector search with empty vector field
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_empty_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "vectorIndex", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 5, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT helio_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT helio_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 3 ] }');
SELECT helio_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 3, "a": "some sentence" }');
SELECT helio_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 4, "a": "some other sentence", "v": [3, 2, 1 ] }');

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "nProbes": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF)SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "nProbes": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "nProbes": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF)SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "nProbes": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

SELECT drop_collection('db','aggregation_pipeline_empty_vector');