SET search_path TO documentdb_api_catalog;

SET documentdb.next_collection_id TO 14200;
SET documentdb.next_collection_index_id TO 14200;

SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl','{ "_id": 1, "year": 2020, "category": "X", "stats": { "2020A": 10, "2020B": 20 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl','{ "_id": 2, "year": 2019, "category": "X", "stats": { "2019A": 30, "2019B": 40, "2019C": 0, "2019D": 0 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl','{ "_id": 3, "year": 2020, "category": "Y", "stats": { "2020A": 50 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl','{ "_id": 4, "year": 2019, "category": "Y", "stats": { "2019C": 60, "2019D": 70 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl','{ "_id": 5, "year": 2020, "category": "Z", "stats": { "2019C": 80, "2019D": 90 } }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$year", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$category", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$year", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$category", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');

SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl2','{ "_id": 13, "group": 1, "obj": {}, "val": null }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl2','{ "_id": 14, "group": 1, "obj": { "x": 2, "y": 2 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl2','{ "_id": 15, "group": 1, "obj": { "x": 1, "z": 3, "y": null } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjectsGroupColl2','{ "_id": 16, "group": 2, "obj": { "x": 1, "y": 1 }, "val": null }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj.x" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": { "result": "$obj.y" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$val" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj.x" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": { "result": "$obj.y" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$val" } } } ] }');

/* shard collections and test for order and validations */
SELECT documentdb_api.shard_collection('db', 'mergeObjectsGroupColl', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$category", "mergedStats": { "$mergeObjects": "$stats" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjectsGroupColl", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "shouldFail": { "$mergeObjects": "$category" } } } ] }');

select documentdb_api.drop_collection('db','mergeObjectsGroupColl');
select documentdb_api.drop_collection('db','mergeObjectsGroupColl2');