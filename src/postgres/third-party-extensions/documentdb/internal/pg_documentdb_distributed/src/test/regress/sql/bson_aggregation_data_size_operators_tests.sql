SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 8500000;
SET documentdb.next_collection_id TO 8500;
SET documentdb.next_collection_index_id TO 8500;

-- $bsonSize operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": {"x":1} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": {}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": [{"a":1}] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": {"$undefined":true} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": {"x":1 , "b": 5, "c": {"$numberLong": "10"} } }}');

-- $bsonSize operator , large size documents
SELECT * FROM bson_dollar_project(('{"input" : "' || LPAD('', 16000000, 'a') || '"}')::bson, '{"result": { "$bsonSize": "$$ROOT" }}');


-- $bsonSize operator , document based tests
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$bsonSize": "$$ROOT" }}');
SELECT * FROM bson_dollar_project('{"a":1, "b":{"a":12} }', '{"result": { "$bsonSize": "$$ROOT" }}');
SELECT * FROM bson_dollar_project('{"a":1, "b":{"a":12} }', '{"result": { "$bsonSize": "$$ROOT.b" }}');

SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$bsonSize": "$$CURRENT" }}');
SELECT * FROM bson_dollar_project('{"a":1, "b":{"a":12} }', '{"result": { "$bsonSize": "$$CURRENT" }}');
SELECT * FROM bson_dollar_project('{"a":1, "b":{"a":12} }', '{"result": { "$bsonSize": "$$CURRENT.b" }}');

-- $bsonSize operator negative tests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": [1,2] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": 11 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": true }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": "12absd" }}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$bsonSize": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$bsonSize": {"$regex": "a*b", "$options":""} }}');

-- $binarySize operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": {"$undefined":true} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "abvd" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "hello!" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "abc\\0c" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "I ❤️ documentdb" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "🙈" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": "アユシュ・サルージャです"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": {"$binary": { "base64": "bGlnaHQgdw==", "subType": "01"}}}}');

-- $binarySize operator , large size documents
SELECT * FROM bson_dollar_project(('{"input" : "' || LPAD('', 16000000, 'a') || '"}')::bson, '{"result": { "$binarySize": "$input" }}');

-- $binarySize operator , document based tests
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$binarySize": "$a" }}');
SELECT * FROM bson_dollar_project('{"a":"temp", "b":{"a":12} }', '{"result": { "$binarySize": "$a" }}');
SELECT * FROM bson_dollar_project('{"a":1, "b":{"a":"world!"} }', '{"result": { "$binarySize": "$b.a" }}');

-- $binarySize operator negative tests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": [1,2] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": 11 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": true }}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$binarySize": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$binarySize": {"$regex": "a*b", "$options":""} }}');
