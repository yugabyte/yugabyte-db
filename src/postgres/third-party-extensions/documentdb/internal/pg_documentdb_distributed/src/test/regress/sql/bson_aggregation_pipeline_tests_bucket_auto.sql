SET search_path TO documentdb_api_catalog;

SET citus.next_shard_id TO 5110000;
SET documentdb.next_collection_id TO 51100;
SET documentdb.next_collection_index_id TO 51100;

/* Insert data */
SELECT documentdb_api.insert_one('db','dollarBucketAuto',' { "_id" : 1, "product" : "apple", "price" : 1, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 2, "product" : "peach", "price" : 2, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto',' { "_id" : 3, "product" : "melon", "price" : 5, "year": 2021}', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto',' { "_id" : 4, "product" : "melon", "price" : 7, "year": 2021}', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 5, "product" : "melon", "price" : 20, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 6, "product" : "apple", "price" : 30, "year": 2022 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 7, "product" : "melon", "price" : 60, "year": 2022 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 8, "product" : "peach", "price" : 62, "year": 2022 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 9, "product" : "banana", "price" : 170, "year": 2023 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 10, "product" : "banana", "price" : 300, "year": 2023 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 11, "product" : "peach", "price" : 320, "year": 2023 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 12, "product" : "peach", "price" : 350, "year": 2024 }', NULL);

/* positive cases: */
-- $bucketAuto with only required fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3 } } ] }');
-- $bucketAuto with diferent num of buckets
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 4 } } ] }');
-- $bucketAuto with output fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "output": { "count": { "$sum": 1 }, "averageAmount": { "$avg": "$price" } } } } ] }');
-- $bucketAuto without count in output fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "output": { "averageAmount": { "$avg": "$price" } } } } ] }');
-- $bucketAuto with expression in groupBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": { "$subtract": ["$price", 1] }, "buckets": 3 } } ] }');
-- $bucketAuto with another stage before it
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$match": { "product": "melon" } }, { "$bucketAuto": { "groupBy": "$price", "buckets": 2 } } ] }');
-- groupBy non-integar field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$product", "buckets": 3 } } ] }');
-- unevenly distributed data
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 5 } } ] }');
-- try granularity
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "granularity": "R5" } } ] }');
-- $bucketAuto with $let
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": { "$let": { "vars": { "adjustedAmount": { "$multiply": ["$price", 10] } }, "in": "$$adjustedAmount" }}, "buckets": 3 } } ] }');
-- With let at aggregation level
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "let": { "multiplier": 10 }, "pipeline": [ { "$bucketAuto": { "groupBy": { "$multiply": ["$price", "$$multiplier"] }, "buckets": 3 } } ] }');

-- less buckets will be returned if the unique values are less than the buckets specified
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$product", "buckets": 8 } } ] }');
-- Expand bucket: same value should go to the same bucket, even if this makes the buckets uneven
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$year", "buckets": 3 } } ] }');

/* all granularity types */
DO $$
DECLARE i int;
BEGIN
FOR i IN 0..99 LOOP
PERFORM documentdb_api.insert_one('db', 'bucketAutoGranularity', FORMAT('{ "_id": %s, "amount": %s }', i + 1, i)::documentdb_core.bson);
END LOOP;
END;
$$;
-- POWERSOF2
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "POWERSOF2" } } ] }');
-- 1-2-5
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "1-2-5" } } ] }');
-- R5
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "R5" } } ] }');
-- R10
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "R10" } } ] }');
-- R20
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "R20" } } ] }');
-- R40
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "R40" } } ] }');
-- R80
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "R80" } } ] }');
-- E6
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E6" } } ] }');
-- E12
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E12" } } ] }');
-- E24
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E24" } } ] }');
-- E48
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E48" } } ] }');
-- E96
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E96" } } ] }');
-- E192
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAutoGranularity", "pipeline": [ { "$bucketAuto": { "groupBy": "$amount", "buckets": 5, "granularity": "E192" } } ] }');


/* groupBy array or document field */
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 1, "valueArray" : [1, 2, 3], "valueDocument" : { "a": 1, "b": 2 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 2, "valueArray" : [4, 5, 6], "valueDocument" : { "a": 3, "b": 4 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 3, "valueArray" : [9, 8], "valueDocument" : { "a": 5, "b": 6 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 4, "valueArray" : [10, 11, 12], "valueDocument" : { "a": 4, "b": 8 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 5, "valueArray" : [3, 14, 15], "valueDocument" : { "a": 2, "b": 10 } }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAutoGroupBy', '{ "_id" : 6, "valueArray" : [6, 17, 18], "valueDocument" : { "a": 11, "b": 12 } }', NULL);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAutoGroupBy", "pipeline": [ { "$bucketAuto": { "groupBy": "$valueArray", "buckets": 3, "output": { "Ids" : { "$push": "$_id" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAutoGroupBy", "pipeline": [ { "$bucketAuto": { "groupBy": "$valueDocument", "buckets": 3, "output": { "Ids" : { "$push": "$_id" } } } } ] }');

/* negative cases, validations: */
-- required fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "buckets": 3 } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price" } } ] }');
-- groupBy must be a path with prefix $ or expression
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "price", "buckets": 3 } } ] }');
-- buckets must be a positive integer
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": "abc" } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3.1 } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": -3 } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 0 } } ] }');
-- output must be document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "output": 1 } } ] }');
-- unknown argument of $bucketAuto
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "unknown": 1 } } ] }');

-- granularity must be a string
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "granularity": 1 } } ] }');
-- unsupported granularity
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "granularity": "abc" } } ] }');
-- when has granularity, groupby value must be a non-negative number
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$product", "buckets": 3, "granularity": "POWERSOF2" } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": { "$subtract": ["$price", 100] }, "buckets": 3, "granularity": "POWERSOF2" } } ] }');

/* Explain */
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3 } } ] }');


/* running $bucketAuto with intermediate size of more than 100mb */
DO $$
DECLARE i int;
BEGIN
-- each doc is "%s": 5 MB - ~5.5 MB & there's 60 of them
FOR i IN 1..60 LOOP
PERFORM documentdb_api.insert_one('db', 'bucketAuto_sizes_test', FORMAT('{ "_id": %s, "groupName": "ABC", "tag": { "%s": [ %s "d" ] } }', i, i, repeat('"' || i || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

/* should fail with intermediate size error */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "bucketAuto_sizes_test", "pipeline": [ { "$bucketAuto": { "groupBy": "$_id", "buckets": 2, "output": { "allTags" : { "$push" : "$tag" } } } } ] }');


/* sharded collection */
SELECT documentdb_api.shard_collection('db', 'dollarBucketAuto', '{ "price": "hashed" }', false);

/* positive cases: */
-- $bucketAuto with only required fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3 } } ] }');
-- $bucketAuto with diferent num of buckets
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 4 } } ] }');
-- $bucketAuto with output fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "output": { "count": { "$sum": 1 }, "averageAmount": { "$avg": "$price" } } } } ] }');
-- $bucketAuto without count in output fields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3, "output": { "averageAmount": { "$avg": "$price" } } } } ] }');
-- $bucketAuto with expression in groupBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": { "$subtract": ["$price", 1] }, "buckets": 3 } } ] }');
-- $bucketAuto with another stage before it
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$match": { "product": "banana" } }, { "$bucketAuto": { "groupBy": "$price", "buckets": 2 } } ] }');
-- groupBy non-integar field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$product", "buckets": 3 } } ] }');
-- unevenly distributed data
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 5 } } ] }');
-- test with null values
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 13, "product" : "peach", "price" : null, "year": 2024 }', NULL);
SELECT documentdb_api.insert_one('db','dollarBucketAuto','{ "_id" : 14, "product" : "peach", "price" : null, "year": 2024 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 5 } } ] }');

/* Explain */
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "dollarBucketAuto", "pipeline": [ { "$bucketAuto": { "groupBy": "$price", "buckets": 3 } } ] }');
