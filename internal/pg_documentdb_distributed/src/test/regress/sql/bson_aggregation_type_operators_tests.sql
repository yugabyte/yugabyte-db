SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 6910000;
SET documentdb.next_collection_id TO 69100;
SET documentdb.next_collection_index_id TO 69100;

-- $isNumber: return true for numbers false otherwise
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": [["not null"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": {"$add": [1, 2]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": "1"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": {"$numberLong": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": {"$numberDecimal": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": {"$numberDouble": "1.0"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": {}}}');

-- $isNumber: returns null with null or undefined
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": "$a"}}');

-- $isNumber: error number of args should be 1
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$isNumber": [[], 2]}}');

-- $type: returns expected type
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "1", "input": {"$numberDouble": "1.0"}, "expected": "double"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "2", "input": "string", "expected": "string"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "3", "input": {}, "expected": "object"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "4", "input": [], "expected": "array"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "5", "input": {"$binary": { "base64": "bGlnaHQgdw==", "subType": "01"}}, "expected": "binData"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "6", "input": {"$oid" : "639926cee6bda3127f153bf1" }, "expected": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "7", "input": true, "expected": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "8", "input": {"$date": "2022-01-01T00:00:00.000Z"}, "expected": "date"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "9", "input": null, "expected": "null"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "10", "input": { "$regex": "a.*b", "$options": "" }, "expected": "regex"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "11", "input": { "$code": "var a = 1;"}, "expected": "javascript"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "12", "input": 1, "expected": "int"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "13", "input": {"$timestamp" : { "t": 1670981326, "i": 1 }}, "expected": "timestamp"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "14", "input": {"$numberLong" : "1"}, "expected": "long"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "15", "input": {"$numberDecimal" : "1"}, "expected": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "16", "input": {"$minKey" : 1}, "expected": "minKey"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "17", "input": {"$maxKey" : 1}, "expected": "maxKey"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "18", "expected": "missing"}');

SELECT bson_dollar_project(document, '{"output": {"$type": "$input"}, "expected": 1}') FROM documentdb_api.collection('db', 'dollarType');

-- $convert: returns expected values
-- double input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "1", "input": {"$numberDouble": "1.9"}, "expected": {"$numberDouble": "1.9"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "2", "input": {"$numberDouble": "1.9"}, "expected": "1.9", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "3", "input": {"$numberDouble": "1.9"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "4", "input": {"$numberDouble": "0.0"}, "expected": false, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "5", "input": {"$numberDouble": "-0.1"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "6", "input": {"$numberDouble": "1.9"}, "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "target": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "7", "input": {"$numberDouble": "1.9"}, "expected": 1, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "8", "input": {"$numberDouble": "1.9"}, "expected": {"$numberLong": "1"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "9", "input": {"$numberDouble": "1.9"}, "expected": {"$numberDecimal": "1.9"}, "target": "decimal"}');
-- str input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "10", "input": "myString", "expected": "myString", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "11", "input": "1.9", "expected": {"$numberDouble": "1.9"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "12", "input": "1E-2", "expected": {"$numberDouble": "0.01"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "13", "input": "13423423E-25", "expected": {"$numberDouble": "1.3423423e-18"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "14", "input": "0000000.0000000", "expected": {"$numberDouble": "0.0"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "15", "input": "1.9", "expected": {"$numberDecimal": "1.9"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "16", "input": "1E-2", "expected": {"$numberDecimal": "0.01"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "17", "input": "-13423423E25", "expected": {"$numberDecimal": "-1.3423423E+32"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "18", "input": "000000.000000", "expected": {"$numberDecimal": "0.000000"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "19", "input": "-1232", "expected": {"$numberLong": "-1232"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "20", "input": "00053235", "expected": {"$numberLong": "53235"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "21", "input": "00053235", "expected": 53235, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "22", "input": "00000000000", "expected": 0, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "23", "input": "0123456789abcdef01234567", "expected": {"$oid": "0123456789abcdef01234567"}, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "24", "input": "", "expected": true, "target": "bool"}');

-- objectId input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "26", "input": {"$oid": "0123456789abcdef01234567"}, "expected": {"$oid": "0123456789abcdef01234567"}, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "27", "input": {"$oid": "0123456789abcdef01234567"}, "expected": "0123456789abcdef01234567", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "28", "input": {"$oid": "0123456789abcdef01234567"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "29", "input": {"$oid": "0123456789abcdef01234567"}, "expected": {"$date": "1970-08-09T22:25:43Z"}, "target": "date"}');
-- bool input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "30", "input": false, "expected": {"$numberDouble": "0"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "31", "input": true, "expected": {"$numberDouble": "1"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "32", "input": false, "expected": {"$numberDecimal": "0"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "33", "input": true, "expected": {"$numberDecimal": "1"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "34", "input": false, "expected": {"$numberLong": "0"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "35", "input": true, "expected": {"$numberLong": "1"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "36", "input": false, "expected": 0, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "37", "input": true, "expected": 1, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "38", "input": false, "expected": "false", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "39", "input": true, "expected": "true", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "40", "input": false, "expected": false, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "45", "input": true, "expected": true, "target": "bool"}');
-- date input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "46", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": {"$date": "1970-01-01T00:00:00.123Z"}, "target": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "47", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": "1970-01-01T00:00:00.123Z", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "48", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "49", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": {"$numberLong": "123"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "50", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": {"$numberDouble": "123"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "51", "input": {"$date": "1970-01-01T00:00:00.123Z"}, "expected": {"$numberDecimal": "123"}, "target": "decimal"}');
-- int input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "52", "input": 1, "expected": 1, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "53", "input": 1, "expected": {"$numberDouble": "1"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "54", "input": -11, "expected": {"$numberDouble": "-11"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "55", "input": 523453, "expected": {"$numberDecimal": "523453"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "56", "input": 523453, "expected": {"$numberLong": "523453"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "57", "input": 1, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "58", "input": 0, "expected": false, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "59", "input": 25, "expected": "25", "target": "string"}');
-- long input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "61", "input": {"$numberLong": "1" }, "expected": {"$numberLong": "1" }, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "62", "input": {"$numberLong": "1" }, "expected": 1, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "63", "input": {"$numberLong": "1" }, "expected": {"$numberDouble": "1"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "64", "input": {"$numberLong": "-11" }, "expected": {"$numberDouble": "-11"}, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "65", "input": {"$numberLong": "523453" }, "expected": {"$numberDecimal": "523453"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "66", "input": {"$numberLong": "523453" }, "expected": {"$numberLong": "523453"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "67", "input": {"$numberLong": "1" }, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "68", "input": {"$numberLong": "0" }, "expected": false, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "69", "input": {"$numberLong": "25" }, "expected": "25", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "70", "input": {"$numberLong": "25" }, "expected": {"$date": "1970-01-01T00:00:00.025Z"}, "target": "date"}');
-- decimal input
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "71", "input": {"$numberDecimal": "1.9"}, "expected": {"$numberDecimal": "1.9"}, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "72", "input": {"$numberDecimal": "1.9"}, "expected": "1.9", "target": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "73", "input": {"$numberDecimal": "1.9"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "74", "input": {"$numberDecimal": "0.0"}, "expected": false, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "75", "input": {"$numberDecimal": "-0.1"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "76", "input": {"$numberDecimal": "1.9"}, "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "target": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "77", "input": {"$numberDecimal": "1.9"}, "expected": 1, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "78", "input": {"$numberDecimal": "1.9"}, "expected": {"$numberLong": "1"}, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "79", "input": {"$numberDecimal": "1.9"}, "expected": {"$numberDouble": "1.9"}, "target": "double"}');
-- other supported types to bool
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "80", "input": {"$minKey": 1}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "81", "input": {"foo": 1}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "82", "input": ["foo", 1], "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "83", "input": {"$binary": { "base64": "bGlnaHQgdw==", "subType": "01"}}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "84", "input": { "$regex": "a.*b", "$options": "" }, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "85", "input": { "$code": "var a = 1;"}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "86", "input": {"$maxKey": 1}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "87", "input": {"$timestamp" : { "t": 1670981326, "i": 1 }}, "expected": true, "target": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "88", "input": { "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}, "expected": true, "target": "bool"}');
-- str to date is not yet supported, will be added with $dateFromString
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "89", "input": "1970-01-01", "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "90", "input": "1970-01-01T00:00:00.001Z", "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "91", "input": "1970-01-01T00:00:00.001+0500", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "92", "input": "1970-01-01 00:00:00.001 +0500", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "93", "input": "1970-01-01T00:00:00.001+5", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "94", "input": "1970-01-01T00:00:00.001+05", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "95", "input": "1970-01-01T00:00:00.001+050", "expected": {"$date": "1969-12-31T23:10:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "96", "input": "1970-01-01T00:00:00.001+0510", "expected": {"$date": "1969-12-31T18:50:00.001Z"}, "target": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "97", "input": "1970-01-01T00:00:00.001+0501", "expected": {"$date": "1969-12-31T18:59:00.001Z"}, "target": "date"}');

-- call convert with the input and target type and check if the output is equal to the expected output.
SELECT bson_dollar_project(document, '{"passed": { "$eq": [{"$convert": { "input": "$input", "to": "$target"}}, "$expected"]}}') FROM documentdb_api.collection('db', 'convertColl');

-- $convert with null to type should return null with and without onNull
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": 1, "to": null}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": 1, "to": "$missingField"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": 1, "to": null, "onNull": "Input was null"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": 1, "to": "$missingField", "onNull": "Input was null"}}}');

-- $convert with null input should return null without onNull and onNull expression if specified even if 'to' is null
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": null, "to": "string"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": "$missingField", "to": "bool"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": null, "to": null, "onNull": "Input was null"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": "$missingField", "to": null, "onNull": "Input was null"}}}');

-- $convert with null and undefined/missing onNull should return empty
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": null, "to": null, "onNull": "$onNullField"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": "$missingField", "to": null, "onNull": "$onNullField"}}}');

-- $convert with error outside of parsing should honor onError
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "1", "input": {"$numberDouble": "1.9"}, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "2", "input": {"$numberDouble": "1.9"}, "target": "array"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "3", "input": {"$numberDouble": "1.9"}, "target": "object"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "4", "input": { "$oid" : "347f000000c1de008ec19ceb" }, "target": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "5", "input": { "$oid" : "347f000000c1de008ec19ceb" }, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "6", "input": { "$oid" : "347f000000c1de008ec19ceb" }, "target": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "7", "input": { "$oid" : "347f000000c1de008ec19ceb" }, "target": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "8", "input": true, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "9", "input": false, "target": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "10", "input": {"$date": "1970-01-01T00:00:00.000Z"}, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "11", "input": {"$date": "1970-01-01T00:00:00.000Z"}, "target": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "12", "input": 1, "target": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "13", "input": 1.9, "target": "minKey"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "14", "input": 1.9, "target": "missing"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "15", "input": 1.9, "target": "object"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "16", "input": 1.9, "target": "array"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "17", "input": 1.9, "target": "binData"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "18", "input": 1.9, "target": "undefined"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "19", "input": 1.9, "target": "null"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "20", "input": 1.9, "target": "regex"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "21", "input": 1.9, "target": "dbPointer"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "22", "input": 1.9, "target": "javascript"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "23", "input": 1.9, "target": "symbol"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "24", "input": 1.9, "target": "javascriptWithScope"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "25", "input": 1.9, "target": "timestamp"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "26", "input": 1.9, "target": "maxKey"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "27", "input": 1, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "28", "input": {"$numberLong": "1"}, "target": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "29", "input": {"$numberDecimal": "1"}, "target": "objectId"}');

SELECT bson_dollar_project(document, '{"output": {"$convert": { "input": "$input", "to": "$target", "onError": "There was an error in $convert"}}}') FROM documentdb_api.collection('db', 'convertCollWithErrors');

-- $convert undefined/missing field used for onError should return empty result
SELECT bson_dollar_project(document, '{"output": {"$convert": { "input": "$input", "to": "$target", "onError": "$missingField"}}}') FROM documentdb_api.collection('db', 'convertCollWithErrors');

-- $convert errors in parsing should be honored even if onError is set
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": { "input": {"$divide": [1, 0]}, "to": "string", "onError": "There was an error in $convert"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": "$myField"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"to": 1, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"myCustomInput": 1, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "to": "someType", "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "to": {"$numberDouble": "1.1"}, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "to": {"$numberDouble": "255"}, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "to": {"$numberDouble": "-2.0"}, "onError": "There was an error"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": 1, "to": [], "onError": "There was an error"}}}');

-- $convert identifies the target types with numbers
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": -1}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 127}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 0}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 1}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 2}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 3}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 4}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 5}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 6}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 7}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 8}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 9}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 10}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 11}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 12}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 13}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 14}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 15}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 16}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {}, "to": 17}}}');

-- double max + 1 and min-1 range $convert, $toLong
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {"$numberDouble" : "9223372036854775296"}, "to": "long"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {"$numberDouble" : "-9223372036854776833"}, "to": "long"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "9223372036854775296"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "-9223372036854776833"}}}');

-- min supported value for $convert, $toLong
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": {"$numberDouble" : "-9223372036854776832"}, "to": "long"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "-9223372036854776832"}}}');
--
--
-- $to* alias tests.
-- call the shorthand convert $to* aliases.
WITH toQuery AS (SELECT bson_dollar_project(document,
'{"expected": 1,
  "output":
    { 
        "$switch": {
            "branches": [
                {"case": {"$eq": ["$target", "double"]}, "then": {"$toDouble": "$input"}},
                {"case": {"$eq": ["$target", "string"]}, "then": {"$toString": "$input"}},
                {"case": {"$eq": ["$target", "objectId"]}, "then": {"$toObjectId": "$input"}},
                {"case": {"$eq": ["$target", "bool"]}, "then": {"$toBool": "$input"}},
                {"case": {"$eq": ["$target", "date"]}, "then": {"$toDate": "$input"}},
                {"case": {"$eq": ["$target", "int"]}, "then": {"$toInt": "$input"}},
                {"case": {"$eq": ["$target", "long"]}, "then": {"$toLong": "$input"}},
                {"case": {"$eq": ["$target", "decimal"]}, "then": {"$toDecimal": "$input"}}
            ]
        }
    }
}') as outDoc FROM documentdb_api.collection('db', 'convertColl'))
SELECT bson_dollar_project(outDoc, '{"passed": {"$eq": ["$output", "$expected"]}}') FROM toQuery;

-- $toObjectId negative tests
-- only supports 24 char hex string, objectId, null/undefined, and requires exactly 1 argument.
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toObjectId": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toObjectId": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toObjectId": "this is a string"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toObjectId": "asdsfsdzzzzsersdfrtghtsz"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toObjectId": "639926cee6bda3127f153bfZ"}}');

-- $toInt negative tests
-- only supports numeric, bool or string and must not overflow/underflow. Takes exactly 1 argument.
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": [{"$date": "2022-01-01T00:00:00.000Z"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDouble": "2147483648"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDouble": "-Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDecimal": "-Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDecimal": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberDecimal": "-2147483649"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": {"$numberLong": "-2147483649"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "-2147483649" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "2147483648" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "This contains a number: 324423" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "    324423" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "324423     " }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "0x01" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toInt": "" }}');

-- $toLong negative tests
-- only support numeric, date, bool or string, must not overflow/underflow. Takes exactly 1 argument.
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "-Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDecimal": "-Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDecimal": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": {"$numberDecimal": "-9223372036854775809"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "-9223372036854775809" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "9223372036854775808" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "This contains a number: 324423" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "    324423" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "324423     " }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "0x01" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toLong": "" }}');

-- $toDouble negative tests
-- only support numeric, date, bool or string, must not overflow/underflow. Takes exactly 1 argument.
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": {"$numberDecimal": "1.8E309"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": {"$numberDecimal": "-1.8E309"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "1.8E309" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "-1.8E309" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "This contains a number: 324423.23" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "    324423.0" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "324423.00001     " }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "0x01" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDouble": "" }}');

-- $toDecimal negative tests
-- only support numeric, date, bool or string, must not overflow/underflow. Takes exactly 1 argument.
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "123423.8E6145" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "-123423.8E6145" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "This contains a number: 324423.23" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "    324423.0" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "324423.00001     " }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "0x01" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDecimal": "" }}');

-- $toString negative tests
-- only support numeric, date, bool, string, objectId. Takes exactly 1 argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toString": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toString": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toString": {"$timestamp" : { "t": 1670981326, "i": 1 }}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toString": [[]]}}');

-- $toDate negative tests
-- only support double, decimal128, long, objectId, timestamp, date. String support will be added with $dateFromString.
-- Numeric, will be added to the Unix epoch 0 and shouldn't exceed int64 limits. Takes exactly 1 argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": [[]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDouble": "9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDecimal": "9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDecimal": "-9223372036854775809"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDouble": "-9223372036854775809"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2022-01-01T00:00:00.000Z"}}');

-- $$toHashedIndexKey tests
--test int
select *from bson_dollar_project('{"tests": 3}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 3 } }');

--test double
select *from bson_dollar_project('{"tests": 3.1}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 3.1 } }');

--test string
select *from bson_dollar_project('{"tests": "abc"}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": "abc" } }');

--test int64
select *from bson_dollar_project('{"tests": 123456789012345678}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 123456789012345678 } }');

--test array
select *from bson_dollar_project('{"tests": [1, 2, 3.1]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [1, 2, 3.1] } }');

--test nested array
select *from bson_dollar_project('{"tests": [1, 2, 3.1,[4, 5], 6]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [1, 2, 3.1,[4, 5], 6] } }');

--test nested object
select *from bson_dollar_project('{"tests": [{"$numberDecimal": "1.2"},3]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [{"$numberDecimal": "1.2"},3] } }');

--test null
select *from bson_dollar_project('{"tests": null}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": null } }');

--test NaN
select *from bson_dollar_project('{"tests": {"$numberDouble": "NaN"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "NaN"} } }');

--test Infinity
select *from bson_dollar_project('{"tests": {"$numberDouble": "Infinity"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "Infinity"} } }');

--test -Infinity
select *from bson_dollar_project('{"tests": {"$numberDouble": "-Infinity"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "-Infinity"} } }');

--test path
select *from bson_dollar_project('{"tests": {"test" : 5}}', '{"result": { "$toHashedIndexKey": "$tests.test" } }');
select *from bson_dollar_project('{"tests": 3}', '{"result": { "$toHashedIndexKey": "$test" } }');
select *from bson_dollar_project('{"tests": {"test" : 5}}', '{"result": { "$toHashedIndexKey": "$tests.tes" } }');

-- $toUUID tests
-- equivalent $convert query should be tested after extended $convert syntax is supported
-- positive tests
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": "123e4567-e89b-12d3-a456-426614174000" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": ["123e4567-e89b-12d3-a456-426614174000"] }}');

-- negative tests
-- invalid string
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": "invalid-uuid-1234" }}');
-- invalid type
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": 1234 }}');
-- invalid count number of array input
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toUUID": ["123e4567-e89b-12d3-a456-426614174000", "123e4567-e89b-12d3-a456-426614174000"] }}');