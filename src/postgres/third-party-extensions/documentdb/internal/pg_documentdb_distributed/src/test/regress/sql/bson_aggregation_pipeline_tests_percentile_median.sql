-- will move to bson_aggregation_pipeline_stage_setWindowFields.sql and remove this file after window operator is done, just like $stddevopo did
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 1033000;
SET documentdb.next_collection_id TO 10330;
SET documentdb.next_collection_index_id TO 10330;

SELECT documentdb_api.insert_one('db','testPercentileAndMedian',' { "_id" : 1, "group": 1, "dataVal": 111, "dataVal2": 111, "dataVal3": {"$numberDecimal": "111"}, "dataVal4": 111, "dataVal5": NaN }', NULL);
SELECT documentdb_api.insert_one('db','testPercentileAndMedian',' { "_id" : 2, "group": 1, "dataVal": 11, "dataVal2": 11, "dataVal3": {"$numberDecimal": "11"}, "dataVal4": 11, "dataVal5": Infinity }', NULL);
SELECT documentdb_api.insert_one('db','testPercentileAndMedian',' { "_id" : 3, "group": 1, "dataVal": 11111, "dataVal2": "string", "dataVal3": {"$numberDecimal": "11111"}, "dataVal4": {"$numberDecimal": "2E+310"}, "dataVal5": Infinity }', NULL);
SELECT documentdb_api.insert_one('db','testPercentileAndMedian',' { "_id" : 4, "group": 1, "dataVal": 1, "dataVal2": 1, "dataVal3": {"$numberDecimal": "1"}, "dataVal4": 1, "dataVal5": -Infinity }', NULL);
SELECT documentdb_api.insert_one('db','testPercentileAndMedian',' { "_id" : 5, "group": 1, "dataVal": 1111, "dataVal2": 1111, "dataVal3": {"$numberDecimal": "1111"}, "dataVal4": 1111, "dataVal5": NaN }', NULL);

-- positive test case for percentile
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95 ], "method": "approximate" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95, 0.5 ], "method": "approximate" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": null, "p": [ 0.95 ], "method": "approximate" } } } } ] }');
-- contain non-numeric value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal2", "p": [ 0.95 ], "method": "approximate" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal2", "p": [ 0.95, 0.5 ], "method": "approximate" } } } } ] }');
-- contain decimal value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal3", "p": [ 0.95 ], "method": "approximate" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal3", "p": [ 0.95, 0.5 ], "method": "approximate" } } } } ] }');
-- data value exceeds double range taken as infinity
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal4", "p": [ 0.95 ], "method": "approximate" } } } } ] }');
-- handle NaN and Infinity values
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal5", "p": [ 1, 0, 0.1, 0.9, 0.4 ], "method": "approximate" } } } } ] }');
-- with $let
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [{"$group": {"_id": null,"percentileVal": {"$percentile": {"p": "$$ps", "input": "$dataVal","method": "approximate"}}}}], "let": {"ps": [0.95, 0.5]}}');

-- positive test case for median
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal", "method": "approximate" } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": null, "method": "approximate" } } } } ] }');
-- contain non-numeric value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal2", "method": "approximate" } } } } ] }');
-- contain decimal value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal3", "method": "approximate" } } } } ] }');
-- data value exceeds double range taken as infinity
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal4", "method": "approximate" } } } } ] }');
-- handle NaN and Infinity values
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal5", "method": "approximate" } } } } ] }');
-- with $let
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [{"$group": {"_id": null,"medianVal": {"$median": {"input": "$$val","method": "approximate"}}}}], "let": {"val": 5}}');

-- negative test case for percentile
-- invalid input document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": 1 } } } ] }');
-- unkonwn field in input document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95 ], "method": "approximate", "unknownField": "unknownValue" } } } } ] }');
-- p value not an array
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": 1.5, "method": "approximate" } } } } ] }');
-- p value is an empty array
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [], "method": "approximate" } } } } ] }');
-- p value is an array with invalid value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 1.5 ], "method": "approximate" } } } } ] }');
-- invalid method
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95 ], "method": "invalid" } } } } ] }');
-- missing input field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "p": [ 0.95 ], "method": "approximate" } } } } ] }');
-- missing p field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "method": "approximate" } } } } ] }');
-- missing method field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95 ] } } } } ] }');

-- negative test case for median
-- invalid input document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": 1 } } } ] }');
-- unkonwn field in input document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal", "method": "approximate", "unknownField": "unknownValue" } } } } ] }');
-- invalid method
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal", "method": "invalid" } } } } ] }');
-- missing input field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "method": "approximate" } } } } ] }');
-- missing method field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal" } } } } ] }');


/* shard collection */
SELECT documentdb_api.shard_collection('db', 'testPercentileAndMedian', '{ "_id": "hashed" }', false);

-- percentile
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "percentileVal": { "$percentile": { "input": "$dataVal", "p": [ 0.95, 0.5 ], "method": "approximate" } } } } ] }');
-- median
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "testPercentileAndMedian", "pipeline": [ { "$group": { "_id": "$group", "medianVal": { "$median": { "input": "$dataVal", "method": "approximate" } } } } ] }');