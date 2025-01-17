SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 8800000;
SET helio_api.next_collection_id TO 8800;
SET helio_api.next_collection_index_id TO 8800;

-- $tsSecond operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": {"$undefined":true} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsSecond": { "$timestamp": { "t": 1234567890, "i": 1 }} }}');

-- $tsSecond operator , input document tests
SELECT * FROM bson_dollar_project('{"a": { "$timestamp": { "t": 1234567890, "i": 1 }} }', '{"result": { "$tsSecond": "$a" }}');

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
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": { "$timestamp": { "t": 1234567890, "i": 1 }} }}');

-- Create Collection
SELECT helio_api.create_collection('db', 'timestampTest');

-- $tsIncrement/$tsSecond operator , input document tests
SELECT helio_api.insert_one('db','timestampTest','{ "symbol": "a", "saleTimestamp": { "$timestamp": { "t": 1622731060, "i": 1 }} }', NULL);
SELECT helio_api.insert_one('db','timestampTest','{ "symbol": "a", "saleTimestamp": { "$timestamp": { "t": 1622731060, "i": 2 }} }', NULL);
SELECT helio_api.insert_one('db','timestampTest','{ "symbol": "b", "saleTimestamp": { "$timestamp": { "t": 1714124193, "i": 1 }} }', NULL);
SELECT helio_api.insert_one('db','timestampTest','{ "symbol": "b", "saleTimestamp": { "$timestamp": { "t": 1714124192, "i": 1 }} }', NULL);
SELECT helio_api.insert_one('db','timestampTest','{ "symbol": "b", "saleTimestamp": { "$timestamp": { "t": 1714124192, "i": 2 }} }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{"aggregate": "timestampTest", "pipeline": [{ "$project": {"_id": 0, "saleTimestamp": 1, "saleIncrement": {"$tsIncrement": "$saleTimestamp"}, "saleSecond": {"$tsSecond": "$saleTimestamp"}}}  ]}');

-- $tsIncrement operator negative tests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": "" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": [1,2] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": 11 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": true }}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$tsIncrement": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tsIncrement": {"$regex": "a*b", "$options":""} }}');
