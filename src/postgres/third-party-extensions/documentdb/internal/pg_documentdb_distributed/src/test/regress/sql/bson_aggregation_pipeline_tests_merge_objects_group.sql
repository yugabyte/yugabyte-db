SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 1220000;
SET documentdb.next_collection_id TO 12200;
SET documentdb.next_collection_index_id TO 12200;

SELECT documentdb_api.insert_one('db','objColl1','{ "_id": 1, "year": 2017, "item": "A", "quantity": { "2017Q1": 1, "2017Q2": 300 } }', NULL);
SELECT documentdb_api.insert_one('db','objColl1','{ "_id": 2, "year": 2016, "item": "A", "quantity": { "2016Q1": 400, "2016Q2": 300, "2016Q3": 0, "2016Q4": 0 } }', NULL);
SELECT documentdb_api.insert_one('db','objColl1','{ "_id": 3, "year": 2017, "item": "B", "quantity": { "2017Q1": 300 } }', NULL);
SELECT documentdb_api.insert_one('db','objColl1','{ "_id": 4, "year": 2016, "item": "B", "quantity": { "2016Q3": 100, "2016Q4": 250 } }', NULL);
SELECT documentdb_api.insert_one('db','objColl1','{ "_id": 5, "year": 2017, "item": "C", "quantity": { "2016Q3": 1200, "2016Q4": 312 } }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');

SELECT documentdb_api.insert_one('db','objColl2','{ "_id": 13, "group": 1, "obj": {}, "c": null }', NULL);
SELECT documentdb_api.insert_one('db','objColl2','{ "_id": 14, "group": 1, "obj": { "a": 2, "b": 2 } }', NULL);
SELECT documentdb_api.insert_one('db','objColl2','{ "_id": 15, "group": 1, "obj": { "a": 1, "c": 3, "b": null } }', NULL);
SELECT documentdb_api.insert_one('db','objColl2','{ "_id": 16, "group": 2, "obj": { "a": 1, "b": 1 }, "c": null }', NULL);

/* running multiple $mergeObjects accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj.a" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": { "result": "$obj.b" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$c" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$obj.a" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": { "result": "$obj.b" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl2", "pipeline": [ { "$group": { "_id": "$group", "mergedDocument": { "$mergeObjects": "$c" } } } ] }');

/* running mergeObjects with intermediate size of more than 100mb */
DO $$
DECLARE i int;
BEGIN
-- each doc is "%s": 5 MB - ~5.5 MB & there's 25 of them
FOR i IN 1..25 LOOP
PERFORM documentdb_api.insert_one('db', 'sizes_test', FORMAT('{ "_id": %s, "groupName": "A", "c": { "%s": [ %s "d" ] } }', i, i, repeat('"' || i || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

/* should fail with intermediate size error */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sizes_test", "pipeline": [ { "$group": { "_id": "$groupName", "mergedDocument": { "$mergeObjects": "$c" } } } ] }');


/* shard collections and test for order and validations */
SELECT documentdb_api.shard_collection('db', 'objColl1', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$item", "mergedobjColl1": { "$mergeObjects": "$quantity" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "lastItemSold": { "$mergeObjects": { "item": "$item" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "objColl1", "pipeline": [ { "$sort": { "item": 1 } }, { "$group": { "_id": "$year", "shouldFail": { "$mergeObjects": "$item" } } } ] }');

select documentdb_api.drop_collection('db','objColl1');
select documentdb_api.drop_collection('db','objColl2');
select documentdb_api.drop_collection('db','sizes_test');
