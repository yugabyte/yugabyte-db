SET search_path TO helio_core,helio_api,helio_api_catalog,mongo_catalog,helio_api_internal;

SET citus.next_shard_id TO 477000;
SET helio_api.next_collection_id TO 4770;
SET helio_api.next_collection_index_id TO 4770;


SELECT insert_one('db','densify','{ "_id": 1, "a": "abc", "cost": 10, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 2, "a": "abc", "cost": 5, "quantity": 507, "date": { "$date": { "$numberLong": "1718841605000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 3, "a": "abc", "cost": 3, "quantity": 503, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 4, "a": "abc", "cost": 7, "quantity": 504, "date": { "$date": { "$numberLong": "1718841615000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 11, "a": "def", "cost": 16, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 12, "a": "def", "cost": 11, "quantity": 507, "date": { "$date": { "$numberLong": "1718841605000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 13, "a": "def", "cost": -5, "quantity": 503, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);
SELECT insert_one('db','densify','{ "_id": 14, "a": "def", "cost": 9, "quantity": 504, "date": { "$date": { "$numberLong": "1718841615000" } } }', NULL);

-- Validations of $densify stage and negative test cases
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": "Not an object" }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": [ ] }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": 1 } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a" } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": "A" } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : "na" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : "full" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : "partition" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1] } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2, 3] } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, { "$date": { "$numberLong": "1718841600000" } }] } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : "full", "step": "Hello" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : "partition", "step": "Hello" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, {"$numberDouble": 2}], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": {"$numberDecimal": "1"} } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": 1, "unit": "sec" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": 1, "unit": "Hour" } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": -1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": 0 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [1, 2], "step": {"$numberDecimal": "-1e1000"} } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "range": { "bounds" : [2, 1], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "partitionByFields": "a", "range": { "bounds" : "full", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "a", "partitionByFields": ["$a"], "range": { "bounds" : "full", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "$a", "partitionByFields": ["a"], "range": { "bounds" : "full", "step": 1 } } }]}');

-- Valid cases that works

-- Check an empty table returns value when used with range based partition
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "empty_table", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [1, 10], "step": 1 } } }]}');

-- Range with lower and upper bounds
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [8, 12], "step": 1.2 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [-10, -5], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [11, 15], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [3, 10], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": [{ "$date" : { "$numberLong" : "1718841610000" } }, { "$date" : { "$numberLong" : "1718841615000" } }], "step": 1, "unit": "second" } } }, {"$project": { "formatted": { "$dateToString": { "date": "$date" } }}}]}')


-- Partition mode
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1, "unit": "second" } } }, {"$project": { "formatted": { "$dateToString": { "date": "$date" } }}}]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 3, "unit": "second" } } }, {"$project": { "formatted": { "$dateToString": { "date": "$date" } }}}]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": { "$numberDecimal": "1.5"} } } }]}');

-- Full mode
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1, "unit": "second" } } }, {"$project": { "a": 1, "formatted": { "$dateToString": { "date": "$date" } }}}]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 3, "unit": "second" } } }, {"$project": { "a": 1, "formatted": { "$dateToString": { "date": "$date" } }}}]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": { "$numberDecimal": "1.5"} } } }]}');


-- Explains

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "empty_table", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [1, 10], "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');


-- Shard collection
SELECT helio_api.shard_collection('db', 'densify', '{"a": "hashed"}', false);

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');


EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');

-- Reshard multikey
SELECT helio_api.shard_collection('db', 'densify', '{"a": "hashed", "quantity": "hashed"}', true);

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": "partition", "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": "full", "step": 1 } } }]}');


EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": [8, 12], "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": "partition", "step": 1 } } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a", "quantity"], "range": { "bounds": "full", "step": 1 } } }]}');


-- Test that internal number of documents generated limits are working
-- Tests inspired from aggregation/sources/densify/generated_limit.js, currently we don't support setParameter so only a unit test should be suffice to test internal limits
SET helio_api.external.internalQueryMaxAllowedDensifyDocs TO 10;
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [0, 11], "step": 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [0, 45], "step": 4 } } }]}');  
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [0, 10], "step": 4 } } }]}'); -- should pass


SELECT insert_one('db','densify_limit','{ "_id": 1, "a": "abc", "cost": 0, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify_limit','{ "_id": 2, "a": "abc", "cost": 12, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');

SELECT insert_one('db','densify_limit','{ "_id": 3, "a": "def", "cost": 0, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify_limit','{ "_id": 4, "a": "def", "cost": 12, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);

-- Test limit works across partitions
SET helio_api.external.internalQueryMaxAllowedDensifyDocs TO 20;

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}');

-- Verify existing documents don't count towards the limit
SELECT insert_one('db','densify_limit','{ "_id": 5, "a": "abc", "cost": 5, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify_limit','{ "_id": 6, "a": "def", "cost": 5, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": "full", "step": 1 } } }]}'); -- Should pass now


-- Check time also works
SELECT drop_collection('db', 'densify_limit') IS NOT NULL;
SELECT insert_one('db','densify_limit','{ "_id": 1, "a": "abc", "cost": 0, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT insert_one('db','densify_limit','{ "_id": 2, "a": "abc", "cost": 12, "quantity": 501, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);

SET helio_api.external.internalQueryMaxAllowedDensifyDocs TO 5;

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "date", "partitionByFields": ["a"], "range": { "bounds": "partition", "step": 1 , "unit": "second"} } }]}');

RESET helio_api.external.internalQueryMaxAllowedDensifyDocs;

-- Test memory limit too
SET helio_api.external.internaldocumentsourcedensifymaxmemorybytes TO 100;
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "densify_limit", "pipeline":  [{"$densify": { "field": "cost", "partitionByFields": ["a"], "range": { "bounds": [0, 40], "step": 1 } } }]}');

RESET helio_api.external.internalDocumentSourceDensifyMaxmemoryBytes;

SHOW helio_api.external.internalQueryMaxAllowedDensifyDocs;
SHOW helio_api.external.internalDocumentSourceDensifyMaxmemoryBytes;