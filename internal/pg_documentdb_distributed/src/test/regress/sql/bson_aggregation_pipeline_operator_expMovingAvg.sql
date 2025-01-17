SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;

SET citus.next_shard_id TO 4156000;
SET helio_api.next_collection_id TO 41560;
SET helio_api.next_collection_index_id TO 41560;

-- insert data
-- positive case
SELECT helio_api.insert_one('db','test1','{ "_id": 1, "name": "p1", "cost": 13, "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test1','{ "_id": 2, "name": "p1", "cost": 15.4, "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test1','{ "_id": 3, "name": "p1", "cost": 12, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test1','{ "_id": 4, "name": "p1", "cost": 11.7, "date": { "$date": { "$numberLong": "1718841600004"}} }');
SELECT helio_api.insert_one('db','test1','{ "_id": 5, "name": "p2", "cost": 82, "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test1','{ "_id": 6, "name": "p2", "cost": 94, "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test1','{ "_id": 7, "name": "p2", "cost": 112, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test1','{ "_id": 8, "name": "p2", "cost": 97.3, "date": { "$date": { "$numberLong": "1718841600004"}} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 2} } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": 0.666} } } } } ] }');

-- N is long
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 12345678901234} } } } } ] }');

-- negative case
-- no sortBy
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 2} } } } } ] }');

-- alpha and N all exist
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": 0.666, "N": 2} } } } } ] }');

-- miss input
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"alpha": 0.666, "N": 2} } } } } ] }');

-- miss alpha and N
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost"} } } } } ] }');

-- incorrect parameter
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpah": 0.666} } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "M": 2} } } } } ] }');

-- N is float
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 2.3} } } } } ] }');

-- alpha >= 1
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": 2} } } } } ] }');

-- alpha <= 0
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test1", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": -2.8} } } } } ] }');

-- data contains null
SELECT helio_api.insert_one('db','test2','{ "_id": 1, "name": "p1", "cost": 13, "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test2','{ "_id": 2, "name": "p1", "cost": null, "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test2','{ "_id": 3, "name": "p1", "cost": 12, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test2','{ "_id": 4, "name": "p1", "cost": 11.7, "date": { "$date": { "$numberLong": "1718841600004"}} }');
SELECT helio_api.insert_one('db','test2','{ "_id": 5, "name": "p2", "cost": null, "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test2','{ "_id": 6, "name": "p2", "cost": 94, "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test2','{ "_id": 7, "name": "p2", "cost": 112, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test2','{ "_id": 8, "name": "p2", "cost": 97.3, "date": { "$date": { "$numberLong": "1718841600004"}} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test2", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 2} } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test2", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": 0.666} } } } } ] }');

-- input contains string
SELECT helio_api.insert_one('db','test3','{ "_id": 1, "name": "p1", "cost": "asd", "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test3','{ "_id": 2, "name": "p1", "cost": 15.4, "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test3','{ "_id": 3, "name": "p1", "cost": 12, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test3','{ "_id": 4, "name": "p1", "cost": "zxc", "date": { "$date": { "$numberLong": "1718841600004"}} }');
SELECT helio_api.insert_one('db','test3','{ "_id": 5, "name": "p2", "cost": 82, "date": { "$date": { "$numberLong": "1718841600001"}}}');
SELECT helio_api.insert_one('db','test3','{ "_id": 6, "name": "p2", "cost": "qwe", "date": { "$date": { "$numberLong": "1718841600002" } } }');
SELECT helio_api.insert_one('db','test3','{ "_id": 7, "name": "p2", "cost": 112, "date": { "$date": { "$numberLong": "1718841600003" } } }');
SELECT helio_api.insert_one('db','test3','{ "_id": 8, "name": "p2", "cost": "vbn", "date": { "$date": { "$numberLong": "1718841600004"}} }');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test3", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "N": 2} } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test3", "pipeline": [ { "$setWindowFields": { "partitionBy": "$name", "sortBy": {"date": 1}, "output":{"expMovingAvgForCost": { "$expMovingAvg":{"input": "$cost", "alpha": 0.666} } } } } ] }');
