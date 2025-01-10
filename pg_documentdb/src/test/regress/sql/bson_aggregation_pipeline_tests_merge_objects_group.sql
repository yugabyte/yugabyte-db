SET search_path TO helio_api_catalog;

SET helio_api.next_collection_id TO 14200;
SET helio_api.next_collection_index_id TO 14200;

SELECT helio_api.insert_one('db','objColl1','{ "_id": 1, "year": 2017, "item": "A", "quantity": { "2017Q1": 1, "2017Q2": 300 } }', NULL);
SELECT helio_api.insert_one('db','objColl1','{ "_id": 2, "year": 2016, "item": "A", "quantity": { "2016Q1": 400, "2016Q2": 300, "2016Q3": 0, "2016Q4": 0 } }', NULL);
SELECT helio_api.insert_one('db','objColl1','{ "_id": 3, "year": 2017, "item": "B", "quantity": { "2017Q1": 300 } }', NULL);
SELECT helio_api.insert_one('db','objColl1','{ "_id": 4, "year": 2016, "item": "B", "quantity": { "2016Q3": 100, "2016Q4": 250 } }', NULL);
SELECT helio_api.insert_one('db','objColl1','{ "_id": 5, "year": 2017, "item": "C", "quantity": { "2016Q3": 1200, "2016Q4": 312 } }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');

SELECT helio_api.insert_one('db','objColl2','{ "_id": 13, "group": 1, "obj": {}, "c": null }', NULL);
SELECT helio_api.insert_one('db','objColl2','{ "_id": 14, "group": 1, "obj": { "a": 2, "b": 2 } }', NULL);
SELECT helio_api.insert_one('db','objColl2','{ "_id": 15, "group": 1, "obj": { "a": 1, "c": 3, "b": null } }', NULL);
SELECT helio_api.insert_one('db','objColl2','{ "_id": 16, "group": 2, "obj": { "a": 1, "b": 1 }, "c": null }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj.a" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": { "result": "$obj.b" } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$c" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj.a" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": { "result": "$obj.b" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$c" } } } ] }');

/* shard collections and test for order and validations */
SELECT helio_api.shard_collection('db', 'objColl1', '{ "_id": "hashed" }', false);

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "shouldFail": { "$mergeObjects": "$item" } } } ] }');

select helio_api.drop_collection('db','objColl1');
select helio_api.drop_collection('db','objColl2');