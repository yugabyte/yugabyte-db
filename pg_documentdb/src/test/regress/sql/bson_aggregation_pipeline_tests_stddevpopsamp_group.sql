SET search_path TO helio_api_catalog;

SET helio_api.next_collection_id TO 15100;
SET helio_api.next_collection_index_id TO 15100;

SELECT helio_api.insert_one('db','tests',' { "_id" : 1, "group": 1, "num" : 4 }');
SELECT helio_api.insert_one('db','tests',' { "_id" : 2, "group": 1, "num" : 7 }');
SELECT helio_api.insert_one('db','tests',' { "_id" : 3, "group": 1, "num" : 13 }');
SELECT helio_api.insert_one('db','tests',' { "_id" : 4, "group": 1, "num" : 16 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/* empty collection */
SELECT helio_api.insert_one('db','empty_col',' {"num": {} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_col", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_col", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*single number value in collection*/
SELECT helio_api.insert_one('db','single_num',' {"num": 1 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_num", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_num", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*two number values in collection*/
SELECT helio_api.insert_one('db','two_nums',' {"num": 1 }');
SELECT helio_api.insert_one('db','two_nums',' {"num": 1 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "two_nums", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "two_nums", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*single char value in collection*/
SELECT helio_api.insert_one('db','single_char',' {"num": "a" }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_char", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_char", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*single number and single char in collection*/
SELECT helio_api.insert_one('db','single_num_char',' {"num": 1 }');
SELECT helio_api.insert_one('db','single_num_char',' {"num": "a" }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_num_char", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_num_char", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*number and char mixed in collection*/
SELECT helio_api.insert_one('db','num_char_mixed',' {"num": 1 }');
SELECT helio_api.insert_one('db','num_char_mixed',' {"num": "a" }');
SELECT helio_api.insert_one('db','num_char_mixed',' {"num": 1 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_char_mixed", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_char_mixed", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*string in collection*/
SELECT helio_api.insert_one('db','num_string',' {"num": "string1" }');
SELECT helio_api.insert_one('db','num_string',' {"num": "string2" }');
SELECT helio_api.insert_one('db','num_string',' {"num": "strign3" }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_string", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_string", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*large number values in collection*/
SELECT helio_api.insert_one('db', 'large_num',' {"num": {"$numberLong": "10000000004"} }');
SELECT helio_api.insert_one('db', 'large_num',' {"num": {"$numberLong": "10000000007"} }');
SELECT helio_api.insert_one('db', 'large_num',' {"num": {"$numberLong": "10000000013"} }');
SELECT helio_api.insert_one('db', 'large_num',' {"num": {"$numberLong": "10000000016"} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "large_num", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "large_num", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*double in collection*/
SELECT helio_api.insert_one('db', 'double',' {"num": {"$numberDouble": "4.0"} }');
SELECT helio_api.insert_one('db', 'double',' {"num": {"$numberDouble": "7.0"} }');
SELECT helio_api.insert_one('db', 'double',' {"num": {"$numberDouble": "13.0"} }');
SELECT helio_api.insert_one('db', 'double',' {"num": {"$numberDouble": "16.0"} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "double", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "double", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*numberDecimal in collection*/
SELECT helio_api.insert_one('db', 'num_decimal',' {"num": {"$numberDecimal": "4"} }');
SELECT helio_api.insert_one('db', 'num_decimal',' {"num": {"$numberDecimal": "7"} }');
SELECT helio_api.insert_one('db', 'num_decimal',' {"num": {"$numberDecimal": "13"} }');
SELECT helio_api.insert_one('db', 'num_decimal',' {"num": {"$numberDecimal": "16"} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_decimal", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_decimal", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*NaN and Infinity*/
SELECT helio_api.insert_one('db', 'single_nan',' {"num": {"$numberDecimal": "NaN" } }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_nan", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_nan", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

SELECT helio_api.insert_one('db', 'nans',' {"num": {"$numberDecimal": "NaN" } }');
SELECT helio_api.insert_one('db', 'nans',' {"num": {"$numberDecimal": "-NaN"} }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nans", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nans", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

SELECT helio_api.insert_one('db','mix_nan',' {  "num" : 4 }');
SELECT helio_api.insert_one('db','mix_nan',' {  "num" : 7 }');
SELECT helio_api.insert_one('db','mix_nan',' {  "num" : 13 }');
SELECT helio_api.insert_one('db','mix_nan',' {  "num" : {"$numberDecimal": "NaN" } }');
SELECT helio_api.insert_one('db','mix_nan',' {  "num" : 16 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mix_nan", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mix_nan", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

SELECT helio_api.insert_one('db', 'single_infinity',' {"num": {"$numberDecimal": "Infinity"} }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_infinity", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_infinity", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

SELECT helio_api.insert_one('db', 'infinities',' {"num": { "$numberDecimal": "Infinity" } }');
SELECT helio_api.insert_one('db', 'infinities',' {"num": { "$numberDecimal": "-Infinity" } }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "infinities", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "infinities", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

SELECT helio_api.insert_one('db','mix_inf',' {  "num" : 4 }');
SELECT helio_api.insert_one('db','mix_inf',' {  "num" : 7 }');
SELECT helio_api.insert_one('db','mix_inf',' {  "num" : 13 }');
SELECT helio_api.insert_one('db','mix_inf',' {  "num" : { "$numberDecimal": "Infinity" } }');
SELECT helio_api.insert_one('db','mix_inf',' {  "num" : 16 }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mix_inf", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mix_inf", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*number overflow*/
SELECT helio_api.insert_one('db','num_overflow',' {  "num" : {"$numberDecimal": "100000004"} }');
SELECT helio_api.insert_one('db','num_overflow',' {  "num" : {"$numberDecimal": "10000000007"} }');
SELECT helio_api.insert_one('db','num_overflow',' {  "num" : {"$numberDecimal": "1000000000000013"} }');
SELECT helio_api.insert_one('db','num_overflow',' {  "num" : {"$numberDecimal": "1000000000000000000"} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_overflow", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_overflow", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/*array test*/
SELECT helio_api.insert_one('db','num_array',' {  "num" : [4, 7, 13, 16] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_array", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "num_array", "pipeline": [ { "$group": { "_id": 1, "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/* shard collection */
SELECT helio_api.shard_collection('db', 'tests', '{ "_id": "hashed" }', false);

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevPop": "$num" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevSamp": "$num" } } } ] }');

/* nagetive tests */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevPop": ["$num"] } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "tests", "pipeline": [ { "$group": { "_id": "$group", "stdDev": { "$stdDevSamp": ["$num"] } } } ] }');