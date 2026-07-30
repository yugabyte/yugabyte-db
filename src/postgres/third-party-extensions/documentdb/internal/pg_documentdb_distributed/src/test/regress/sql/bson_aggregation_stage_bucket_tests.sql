SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 5120000;
SET documentdb.next_collection_id TO 51200;
SET documentdb.next_collection_index_id TO 51200;

-- Insert data
SELECT documentdb_api.insert_one('db','dollarBucket',' { "_id" : 1, "product" : "almonds", "pricing" : { "bulk": 10, "store": 15 }, "stock" : 2, "year": 2020 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket','{ "_id" : 2, "product" : "peach", "pricing" : { "bulk": 10, "store": 9 }, "stock" : 1, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket',' { "_id" : 3, "product" : "banana", "pricing" : { "bulk": 10, "store": 15 }, "stock" : 5 , "year": 2020}', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket',' { "_id" : 4, "product" : "melon", "pricing" : { "bulk": 4, "store": 10 }, "stock" : 3 , "year": 2022}', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket','{ "_id" : 5, "product" : "banana", "pricing" : { "bulk": 75, "store": 100 }, "stock" : 1, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket','{ "_id" : 6, "product" : "banana", "pricing" : { "bulk": 75, "store": 100 }, "stock" : 1, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucket','{ "_id" : 7, "product" : "banana", "pricing" : { "store": 15, "bulk": 10 }, "stock" : 1, "year": 2020 }', NULL);

-- positive cases:
-- $bucket with only required fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket with default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others" } } ] }');
-- $bucket with output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "count": { "$sum": 1 }, "averageStock": { "$avg": "$stock" } } } } ] }');
-- $bucket with output fields and default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageStock": { "$avg": "$stock" } } } } ] }');
-- $bucket with nested field path in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$pricing.bulk", "boundaries": [10, 20, 30], "default": "unknown" } } ] }');
-- $bucket with expression in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');
-- $bucket with another stage before it
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$match": { "product": "banana" } }, { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket without count in output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "averageStock": { "$avg": "$stock" }, "totalStock": { "$sum": "$stock" } } } } ] }');
-- $bucket with default value equals to the highest boundaries value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021], "default": 2021 } } ] }');
-- groupBy non-integar field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$product", "boundaries": ["a", "c", "n"], "default": "others" } } ] }');

-- groupBy array or document field
SELECT documentdb_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 1, "valueArray" : [1, 2, 3], "valueDocument" : { "a": 1, "b": 2 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 2, "valueArray" : [4, 5, 6], "valueDocument" : { "a": 3, "b": 4 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketGroupBy', '{ "_id" : 3, "valueArray" : [9, 8], "valueDocument" : { "a": 5, "b": 6 } }', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketGroupBy", "pipeline": [ { "$bucket": { "groupBy": "$valueArray", "boundaries": [[0], [5], [10]] } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketGroupBy", "pipeline": [ { "$bucket": { "groupBy": "$valueDocument", "boundaries": [{"a": 0}, {"a": 5}, {"a": 10}] } } ] }');


-- negative cases, validations:
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
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "averageStock": { "$avg": "$stock" }}, "unknown": 1 } } ] }');
-- document cannot fall into any bucket with no default being set.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021] } } ] }');


-- sharded collection
SELECT documentdb_api.shard_collection('db', 'dollarBucket', '{ "_id": "hashed" }', false);

-- $bucket with only required fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket with default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others" } } ] }');
-- $bucket with output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "count": { "$sum": 1 }, "averageStock": { "$avg": "$stock" } } } } ] }');
-- $bucket with output fields and default value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022], "default": "others", "output": { "count": { "$sum": 1 }, "averageStock": { "$avg": "$stock" } } } } ] }');
-- $bucket with nested field path in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$pricing.bulk", "boundaries": [10, 20, 30], "default": "unknown" } } ] }');
-- $bucket with expression in groupBy field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": { "$subtract": ["$year", 2019] }, "boundaries": [1, 2, 3, 4] } } ] }');
-- $bucket with another stage before it
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$match": { "product": "banana" } }, { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023] } } ] }');
-- $bucket without count in output fields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021, 2022, 2023], "output": { "averageStock": { "$avg": "$stock" }, "totalStock": { "$sum": "$stock" } } } } ] }');
-- $bucket with default value equals to the highest boundaries value
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$year", "boundaries": [2020, 2021], "default": 2021 } } ] }');
-- groupBy non-integar field
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucket", "pipeline": [ { "$bucket": { "groupBy": "$product", "boundaries": ["a", "c", "n"], "default": "others" } } ] }');
