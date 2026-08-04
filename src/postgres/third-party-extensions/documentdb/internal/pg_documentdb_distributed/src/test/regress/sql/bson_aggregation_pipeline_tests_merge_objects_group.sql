SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 1220000;
SET documentdb.next_collection_id TO 12200;
SET documentdb.next_collection_index_id TO 12200;

SELECT documentdb_api.insert_one('db','mergeObjTestColl1','{ "_id": 1, "year": 2020, "category": "X", "metrics": { "2020A": 10, "2020B": 200 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl1','{ "_id": 2, "year": 2019, "category": "X", "metrics": { "2019A": 150, "2019B": 250, "2019C": 0, "2019D": 0 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl1','{ "_id": 3, "year": 2020, "category": "Y", "metrics": { "2020A": 250 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl1','{ "_id": 4, "year": 2019, "category": "Y", "metrics": { "2019C": 80, "2019D": 180 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl1','{ "_id": 5, "year": 2020, "category": "Z", "metrics": { "2019C": 900, "2019D": 210 } }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$category", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$category", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');

SELECT documentdb_api.insert_one('db','mergeObjTestColl2','{ "_id": 13, "group": 1, "obj": {}, "val": null }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl2','{ "_id": 14, "group": 1, "obj": { "x": 2, "y": 2 } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl2','{ "_id": 15, "group": 1, "obj": { "x": 1, "z": 3, "y": null } }', NULL);
SELECT documentdb_api.insert_one('db','mergeObjTestColl2','{ "_id": 16, "group": 2, "obj": { "x": 1, "y": 1 }, "val": null }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj.x" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": { "result": "$obj.y" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$val" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$obj.x" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": { "result": "$obj.y" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedObj": { "$mergeObjects": "$val" } } } ] }');

/* running mergeObjects with intermediate size of more than 100mb */
DO $$
DECLARE i int;
BEGIN
-- each doc is "%s": 5 MB - ~5.5 MB & there's 25 of them
FOR i IN 1..25 LOOP
PERFORM documentdb_api.insert_one('db', 'mergeObjSizeTest', FORMAT('{ "_id": %s, "groupName": "A", "largeObj": { "%s": [ %s "d" ] } }', i, i, repeat('"' || i || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

/* should fail with intermediate size error */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjSizeTest", "pipeline": [ { "$group": { "_id": "$groupName", "mergedObj": { "$mergeObjects": "$largeObj" } } } ] }');


/* shard collections and test for order and validations */
SELECT documentdb_api.shard_collection('db', 'mergeObjTestColl1', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$category", "mergedMetrics": { "$mergeObjects": "$metrics" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "lastCategory": { "$mergeObjects": { "category": "$category" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mergeObjTestColl1", "pipeline": [ { "$sort": { "category": 1 } }, { "$group": { "_id": "$year", "shouldFail": { "$mergeObjects": "$category" } } } ] }');

select documentdb_api.drop_collection('db','mergeObjTestColl1');
select documentdb_api.drop_collection('db','mergeObjTestColl2');
select documentdb_api.drop_collection('db','mergeObjSizeTest');

