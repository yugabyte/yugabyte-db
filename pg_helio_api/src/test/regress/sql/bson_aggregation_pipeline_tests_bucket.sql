SET search_path TO helio_api,helio_core,helio_api_catalog;

SET helio_api.next_collection_id TO 5100;
SET helio_api.next_collection_index_id TO 5100;

/* Insert data */
SELECT helio_api.insert_one('db','dollarBucket',' { "_id" : 1, "item" : "almonds", "pricing" : { "wholesale": 10, "retail": 15 }, "quantity" : 2, "year": 2020 }', NULL);
SELECT helio_api.insert_one('db','dollarBucket','{ "_id" : 2, "item" : "pecans", "pricing" : { "wholesale": 10, "retail": 9 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','dollarBucket',' { "_id" : 3, "item" : "bread", "pricing" : { "wholesale": 10, "retail": 15 }, "quantity" : 5 , "year": 2020}', NULL);
SELECT helio_api.insert_one('db','dollarBucket',' { "_id" : 4, "item" : "meat", "pricing" : { "wholesale": 4, "retail": 10 }, "quantity" : 3 , "year": 2022}', NULL);
SELECT helio_api.insert_one('db','dollarBucket','{ "_id" : 5, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','dollarBucket','{ "_id" : 6, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','dollarBucket','{ "_id" : 7, "item" : "bread", "pricing" : { "retail": 15, "wholesale": 10 }, "quantity" : 1, "year": 2020 }', NULL);

/* positive cases: */
-- $bucket with only required fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket with default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others" } } ] }');
-- $bucket with output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
-- $bucket with output fields and default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
-- $bucket with nested field path in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$pricing.wholesale", "boundaries": [10, 20, 30], "default": "unknown" } } ] }');
-- $bucket with expression in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');
-- $bucket with another stage before it
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$match": { "item": "bread" } }, { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket without count in output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "averageQuantity": { "$avg": "$quantity" }, "totalQuantity": { "$sum": "$quantity" } } } } ] }');
-- $bucket with default value equals to the highest boundaries value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021], "default": 2021 } } ] }');
-- groupBy non-integar field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$item", "boundaries": ["a", "c", "n"], "default": "others" } } ] }');

/* groupBy array or document field */
SELECT helio_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 1, "valueArray" : [1, 2, 3], "valueDocument" : { "a": 1, "b": 2 } }', NULL);
SELECT helio_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 2, "valueArray" : [4, 5, 6], "valueDocument" : { "a": 3, "b": 4 } }', NULL);
SELECT helio_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 3, "valueArray" : [9, 8], "valueDocument" : { "a": 5, "b": 6 } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketGroupBy", "pipeline": [ { "$bucket": { "groupBy": "$valueArray", "boundaries": [[0], [5], [10]] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketGroupBy", "pipeline": [ { "$bucket": { "groupBy": "$valueDocument", "boundaries": [{"a": 0}, {"a": 5}, {"a": 10}] } } ] }');

/* negative cases, validations: */
-- required fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "boundaries": [2020, 2021,2022,2023] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year"} } ] }');
-- groupBy must be a path with prefix $ or expression
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- boundaries is acsending constant array, more than one element, same type.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": 2020 } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2020, 2022, 2023] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 1999, 2023] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, "a", 2022, 2023] } } ] }');
-- default must be a constant, the default value must be less than the lowest boundaries value, or greater than or equal to the highest boundaries value, if having same type.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "$pricing"  } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021], "default": 2020 } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2022, 2023], "default": 2021 } } ] }');
-- output must be document
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "output": 1 } } ] }');
-- More validations
-- unknown argument of $bucket
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "averageQuantity": { "$avg": "$quantity" }}, "unknown": 1 } } ] }');
-- document cannot fall into any bucket with no default being set.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021] } } ] }');


/* Explain */
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');


/* running $bucket with intermediate size of more than 100mb */
DO $$
DECLARE i int;
BEGIN
-- each doc is "%s": 5 MB - ~5.5 MB & there's 50 of them
FOR i IN 1..50 LOOP
PERFORM helio_api.insert_one('db', 'bucket_sizes_test', FORMAT('{ "_id": %s, "groupName": "ABC", "tag": { "%s": [ %s "d" ] } }', i, i, repeat('"' || i || repeat('a', 1000) || '", ', 5000))::helio_core.bson);
END LOOP;
END;
$$;

/* should fail with intermediate size error */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bucket_sizes_test", "pipeline": [ { "$bucket": { "groupBy": "$_id", "boundaries": [0, 10, 20, 50, 100], "output": { "allTags" : { "$push" : "$tag" } } } } ] }');


/* sharded collection */
SELECT helio_api.shard_collection('db', 'dollarBucket', '{ "_id": "hashed" }', false);

-- $bucket with only required fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket with default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others" } } ] }');
-- $bucket with output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
-- $bucket with output fields and default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
-- $bucket with nested field path in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$pricing.wholesale", "boundaries": [10, 20, 30], "default": "unknown" } } ] }');
-- $bucket with expression in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');
-- $bucket with another stage before it
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$match": { "item": "bread" } }, { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket without count in output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "averageQuantity": { "$avg": "$quantity" }, "totalQuantity": { "$sum": "$quantity" } } } } ] }');
-- $bucket with default value equals to the highest boundaries value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021], "default": 2021 } } ] }');
-- groupBy non-integar field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$item", "boundaries": ["a", "c", "n"], "default": "others" } } ] }');

/* Explain */
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageQuantity": { "$avg": "$quantity" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');
