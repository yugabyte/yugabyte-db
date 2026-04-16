SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 8800000;
SET documentdb.next_collection_id TO 8800;
SET documentdb.next_collection_index_id TO 8800;

-- $tsSecond operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": {"$undefined":true} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": { "$timestamp": { "t": 1622431468, "i": 1 }} }}');

-- $tsSecond operator , input document tests
SELECT * FROM bson_dollar_project('{"a": { "$timestamp": { "t": 1622431468, "i": 1 }} }', '{"result": { "$tsSecond": "$a" }}');

-- $tsSecond operator negative tests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": "" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": [1,2] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": 11 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": true }}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$tsSecond": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": {"$regex": "a*b", "$options":""} }}');

-- $tsIncrement operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": {"$undefined":true} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": { "$timestamp": { "t": 1622431468, "i": 1 }} }}');

-- Create Collection
SELECT documentdb_api.create_collection('db', 'timestampTest');

-- $tsIncrement/$tsSecond operator , input document tests
SELECT documentdb_api.insert_one('db','timestampTest','{ "symbol": "a", "tsValue": { "$timestamp": { "t": 1622431468, "i": 1 }} }', NULL);
SELECT documentdb_api.insert_one('db','timestampTest','{ "symbol": "a", "tsValue": { "$timestamp": { "t": 1622431468, "i": 2 }} }', NULL);
SELECT documentdb_api.insert_one('db','timestampTest','{ "symbol": "b", "tsValue": { "$timestamp": { "t": 1714124193, "i": 1 }} }', NULL);
SELECT documentdb_api.insert_one('db','timestampTest','{ "symbol": "b", "tsValue": { "$timestamp": { "t": 1714124192, "i": 1 }} }', NULL);
SELECT documentdb_api.insert_one('db','timestampTest','{ "symbol": "b", "tsValue": { "$timestamp": { "t": 1714124192, "i": 2 }} }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{"aggregate": "timestampTest", "pipeline": [{ "$project": {"_id": 0, "tsValue": 1, "incrementValue": {"$tsIncrement": "$tsValue"}, "secondValue": {"$tsSecond": "$tsValue"}}}  ]}');

-- $tsIncrement operator negative tests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": "" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": [1,2] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": 11 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": true }}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$tsIncrement": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": {"$regex": "a*b", "$options":""} }}');
