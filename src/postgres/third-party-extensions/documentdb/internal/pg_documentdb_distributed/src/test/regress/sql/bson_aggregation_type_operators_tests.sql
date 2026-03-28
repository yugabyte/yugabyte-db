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
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "1", "value": {"$numberDouble": "1.0"}, "expected": "double"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "2", "value": "string", "expected": "string"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "3", "value": {}, "expected": "object"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "4", "value": [], "expected": "array"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "5", "value": {"$binary": { "base64": "bGlnaHQgdw==", "subType": "01"}}, "expected": "binData"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "6", "value": {"$oid" : "639926cee6bda3127f153bf1" }, "expected": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "7", "value": true, "expected": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "8", "value": {"$date": "2022-01-01T00:00:00.000Z"}, "expected": "date"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "9", "value": null, "expected": "null"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "10", "value": { "$regex": "a.*b", "$options": "" }, "expected": "regex"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "11", "value": { "$code": "var a = 1;"}, "expected": "javascript"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "12", "value": 1, "expected": "int"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "13", "value": {"$timestamp" : { "t": 1670981326, "i": 1 }}, "expected": "timestamp"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "14", "value": {"$numberLong" : "1"}, "expected": "long"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "15", "value": {"$numberDecimal" : "1"}, "expected": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "16", "value": {"$minKey" : 1}, "expected": "minKey"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "17", "value": {"$maxKey" : 1}, "expected": "maxKey"}');
SELECT * from documentdb_api.insert_one('db', 'dollarType', '{"_id": "18", "expected": "missing"}');

SELECT bson_dollar_project(document, '{"output": {"$type": "$value"}, "expected": 1}') FROM documentdb_api.collection('db', 'dollarType');

-- $convert: returns expected values
-- double value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "1", "value": {"$numberDouble": "1.3"}, "expected": {"$numberDouble": "1.3"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "2", "value": {"$numberDouble": "1.3"}, "expected": "1.3", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "3", "value": {"$numberDouble": "1.3"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "4", "value": {"$numberDouble": "0.0"}, "expected": false, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "5", "value": {"$numberDouble": "-0.1"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "6", "value": {"$numberDouble": "1.3"}, "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "convertTo": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "7", "value": {"$numberDouble": "1.3"}, "expected": 1, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "8", "value": {"$numberDouble": "1.3"}, "expected": {"$numberLong": "1"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "9", "value": {"$numberDouble": "1.3"}, "expected": {"$numberDecimal": "1.3"}, "convertTo": "decimal"}');
-- str value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "10", "value": "myString", "expected": "myString", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "11", "value": "1.3", "expected": {"$numberDouble": "1.3"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "12", "value": "1E-2", "expected": {"$numberDouble": "0.01"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "13", "value": "13423423E-25", "expected": {"$numberDouble": "1.3423423e-18"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "14", "value": "0000000.0000000", "expected": {"$numberDouble": "0.0"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "15", "value": "1.3", "expected": {"$numberDecimal": "1.3"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "16", "value": "1E-2", "expected": {"$numberDecimal": "0.01"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "17", "value": "-13423423E25", "expected": {"$numberDecimal": "-1.3423423E+32"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "18", "value": "000000.000000", "expected": {"$numberDecimal": "0.000000"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "19", "value": "-1232", "expected": {"$numberLong": "-1232"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "20", "value": "00053235", "expected": {"$numberLong": "53235"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "21", "value": "00053235", "expected": 53235, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "22", "value": "00000000000", "expected": 0, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "23", "value": "665a1c0f8a2b4f7cd317a2b4", "expected": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "24", "value": "", "expected": true, "convertTo": "bool"}');

-- objectId value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "26", "value": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "expected": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "27", "value": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "expected": "665a1c0f8a2b4f7cd317a2b4", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "28", "value": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "29", "value": {"$oid": "665a1c0f8a2b4f7cd317a2b4"}, "expected": {"$date": "2024-05-31T18:50:55.000Z"}, "convertTo": "date"}');
-- bool value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "30", "value": false, "expected": {"$numberDouble": "0"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "31", "value": true, "expected": {"$numberDouble": "1"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "32", "value": false, "expected": {"$numberDecimal": "0"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "33", "value": true, "expected": {"$numberDecimal": "1"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "34", "value": false, "expected": {"$numberLong": "0"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "35", "value": true, "expected": {"$numberLong": "1"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "36", "value": false, "expected": 0, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "37", "value": true, "expected": 1, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "38", "value": false, "expected": "false", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "39", "value": true, "expected": "true", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "40", "value": false, "expected": false, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "45", "value": true, "expected": true, "convertTo": "bool"}');
-- date value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "46", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": {"$date": "2024-05-31T18:50:55.000Z"}, "convertTo": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "47", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": "2024-05-31T18:50:55.000Z", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "48", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "49", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": {"$numberLong": "1717181455000"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "50", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": {"$numberDouble": "1717181455000"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "51", "value": {"$date": "2024-05-31T18:50:55.000Z"}, "expected": {"$numberDecimal": "1717181455000"}, "convertTo": "decimal"}');
-- int value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "52", "value": 1, "expected": 1, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "53", "value": 1, "expected": {"$numberDouble": "1"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "54", "value": -11, "expected": {"$numberDouble": "-11"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "55", "value": 523453, "expected": {"$numberDecimal": "523453"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "56", "value": 523453, "expected": {"$numberLong": "523453"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "57", "value": 1, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "58", "value": 0, "expected": false, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "59", "value": 25, "expected": "25", "convertTo": "string"}');
-- long value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "61", "value": {"$numberLong": "1" }, "expected": {"$numberLong": "1" }, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "62", "value": {"$numberLong": "1" }, "expected": 1, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "63", "value": {"$numberLong": "1" }, "expected": {"$numberDouble": "1"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "64", "value": {"$numberLong": "-11" }, "expected": {"$numberDouble": "-11"}, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "65", "value": {"$numberLong": "523453" }, "expected": {"$numberDecimal": "523453"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "66", "value": {"$numberLong": "523453" }, "expected": {"$numberLong": "523453"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "67", "value": {"$numberLong": "1" }, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "68", "value": {"$numberLong": "0" }, "expected": false, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "69", "value": {"$numberLong": "25" }, "expected": "25", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "70", "value": {"$numberLong": "25" }, "expected": {"$date": "1970-01-01T00:00:00.025Z"}, "convertTo": "date"}');
-- decimal value
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "71", "value": {"$numberDecimal": "1.3"}, "expected": {"$numberDecimal": "1.3"}, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "72", "value": {"$numberDecimal": "1.3"}, "expected": "1.3", "convertTo": "string"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "73", "value": {"$numberDecimal": "1.3"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "74", "value": {"$numberDecimal": "0.0"}, "expected": false, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "75", "value": {"$numberDecimal": "-0.1"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "76", "value": {"$numberDecimal": "1.3"}, "expected": {"$date": "1970-01-01T00:00:00.001Z"}, "convertTo": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "77", "value": {"$numberDecimal": "1.3"}, "expected": 1, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "78", "value": {"$numberDecimal": "1.3"}, "expected": {"$numberLong": "1"}, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "79", "value": {"$numberDecimal": "1.3"}, "expected": {"$numberDouble": "1.3"}, "convertTo": "double"}');
-- other supported types to bool
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "80", "value": {"$minKey": 1}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "81", "value": {"foo": 1}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "82", "value": ["foo", 1], "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "83", "value": {"$binary": { "base64": "bGlnaHQgdw==", "subType": "01"}}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "84", "value": { "$regex": "a.*b", "$options": "" }, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "85", "value": { "$code": "var a = 1;"}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "86", "value": {"$maxKey": 1}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "87", "value": {"$timestamp" : { "t": 1670981326, "i": 1 }}, "expected": true, "convertTo": "bool"}');
SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "88", "value": { "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}, "expected": true, "convertTo": "bool"}');
-- str to date is not yet supported, will be added with $dateFromString
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "89", "value": "2024-05-31", "expected": {"$date": "2024-05-31T00:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "90", "value": "2024-05-31T00:00:00.001Z", "expected": {"$date": "2024-05-31T00:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "91", "value": "2024-05-31T00:00:00.001+0500", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "92", "value": "2024-05-31 00:00:00.001 +0500", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "93", "value": "2024-05-31T00:00:00.001+5", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "94", "value": "2024-05-31T00:00:00.001+05", "expected": {"$date": "1969-12-31T19:00:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "95", "value": "2024-05-31T00:00:00.001+050", "expected": {"$date": "1969-12-31T23:10:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "96", "value": "2024-05-31T00:00:00.001+0510", "expected": {"$date": "1969-12-31T18:50:00.001Z"}, "convertTo": "date"}');
-- SELECT * from documentdb_api.insert_one('db', 'convertColl', '{"_id": "97", "value": "2024-05-31T00:00:00.001+0501", "expected": {"$date": "1969-12-31T18:59:00.001Z"}, "convertTo": "date"}');

-- call convert with the value and convertTo and check if the output is equal to the expected output.
SELECT bson_dollar_project(document, '{"result": { "$eq": [{"$convert": { "input": "$value", "to": "$convertTo"}}, "$expected"]}}') FROM documentdb_api.collection('db', 'convertColl');

-- $convert to BinData
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": "binData", "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "RG9jdW1lbnREQiBpcyBjb29sbGwh", "to": {"type": "binData", "subtype": 8}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": {"type": "binData", "subtype": 0}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "RG9jdW1lbnREQiBpcyBjb29sbGwh", "to": {"type": "binData", "subtype": 0}, "format": "base64url"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "UEdNb25nbyBpcyBHQSE", "to": {"type": "binData", "subtype": 0}, "format": "base64url"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": {"type": "binData", "subtype": 0}, "format": "base64url"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "10000000-1000-1000-1000-100000000000", "to": {"type": "binData", "subtype": 4}, "format": "uuid"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "451e4567-e89b-42d3-a456-556642440000", "to": {"type": "binData", "subtype": 4}, "format": "uuid"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "4a6f686e20446f65", "to": {"type": "binData", "subtype": 0}, "format": "hex"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "4a6f686e20446f65", "to": {"type": "binData", "subtype": 5}, "format": "hex"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "4a6f686e20446f65", "to": {"type": "binData", "subtype": 6}, "format": "hex"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "a27", "to": {"type": "binData", "subtype": 0}, "format": "utf8"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "UEdNb25nbyBpcyBHQSE=", "to": {"type": "binData"}, "format": "utf8"}}}');
SELECT * from bson_dollar_project('{"i": "167dee52-c331-484e-92d1-c56479b8e670"}', '{"result": {"$convert": { "input": "$i", "to": {"type": "binData", "subtype": 4}, "format": "uuid"}}}');
SELECT * from bson_dollar_project('{"i": "SGVsbG8gd29ybGQh", "ts": 0 }', '{"result": {"$convert": { "input": "$i", "to": {"type": "binData", "subtype": "$ts"}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{"tt": "binData", "f": "hex"}', '{"result": {"$convert": { "input": "4a6f686e20446f65", "to": {"type": "$tt", "subtype": 0}, "format": "$f"}}}');

-- error: $convert to BinData with no format
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": "binData"}}}');

-- error: $convert to BinData with non-string input
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": 123, "to": "binData", "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": false, "to": "binData", "format": "base64"}}}');

-- error: uuid format and uuid subtype violations
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": {"type": "binData", "subtype": 4}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "451e4567-e89b-42d3-a456-556642440000", "to": {"type": "binData"}, "format": "uuid"}}}');

-- error: $convert to BinData with input-format mismatch
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "123", "to": "binData", "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "hn3uUsMxSE6S0cVkebjm=fg==", "to": {"type": "binData", "subtype": 4}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "451e4567-e89b-42d3-a456-556642440000", "to": {"type": "binData", "subtype": 0}, "format": "base64"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "hn3u+UsMxSE6S0cVkebjmfg==", "to": {"type": "binData", "subtype": 0}, "format": "base64url"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "hn3u+UsMxSE6S0cVkebjm/fg==", "to": {"type": "binData", "subtype": 0}, "format": "base64url"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "äöäöä", "to": {"type": "binData", "subtype": 4}, "format": "uuid"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": {"type": "binData", "subtype": 4}, "format": "uuid"}}}');
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "451e4567-e89b-42d3-a456-556642440000", "to": {"type": "binData", "subtype": 4}, "format": "hex"}}}');

-- error: auto format not supported for to BinData
SELECT * from bson_dollar_project('{}', '{"result": {"$convert": { "input": "SGVsbG8gd29ybGQh", "to": {"type": "binData", "subtype": 0}, "format": "auto"}}}');

-- error: auto format not supported for BinData to string yet
SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "4a6f686e20446f65",
                "to": {"type": "binData", "subtype": 5},
                "format": "hex"
            }
        },
        "to": "string",
        "format": "auto"
    }
}}');
 
-- BinData to string using $convert
SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "SGVsbG8gd29ybGQh",
                "to": "binData",
                "format": "base64"
            }
        },
        "to": "string",
        "format": "base64"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "SGVsbG8gd29ybGQh",
                "to": "binData",
                "format": "base64"
            }
        },
        "to": "string",
        "format": "utf8"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "💻🤠🔫",
                "to": "binData",
                "format": "utf8"
            }
        },
        "to": "string",
        "format": "utf8"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "SGVsbG8gd29ybGQh",
                "to": "binData",
                "format": "base64"
            }
        },
        "to": "string",
        "format": "base64url"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "167dee52-c331-484e-92d1-c56479b8e670",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": "string",
        "format": "uuid"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "SGVsbG8gd29ybGQh",
                "to": "binData",
                "format": "base64"
            }
        },
        "to": "string",
        "format": "hex"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "10000000-1000-1000-1000-100000000000",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": "string",
        "format": "uuid"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "4a6f686e20446f65",
                "to": "binData",
                "format": "hex"
            }
        },
        "to": "string",
        "format": "hex"
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "4a6f686e20446f65",
                "to": {"type": "binData", "subtype": 5},
                "format": "hex"
            }
        },
        "to": "string",
        "format": "hex"
    }
}}');

SELECT * FROM bson_dollar_project('{"i": "a27", "tt": "binData", "f": "hex"}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "$i",
                "to": {"type": "$tt", "subtype": 0},
                "format": "$f"
            }
        },
        "to": "string",
        "format": "$f"
    }
}}');

SELECT * FROM bson_dollar_project('{"i": "167dee52-c331-484e-92d1-c56479b8e670"}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "$i",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": "string",
        "format": "uuid"
    }
}}');

-- $convert from BinData to BinData
SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "SGVsbG8gd29ybGQh",
                "to": "binData",
                "format": "base64"
            }
        },
        "to":  {"type": "binData", "subtype": 4}
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "💻🤠🔫",
                "to": "binData",
                "format": "utf8"
            }
        },
        "to": {"type": "binData", "subtype": 0}
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "10000000-1000-1000-1000-100000000000",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": {"type": "binData", "subtype": 0}
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "10000000-1000-1000-1000-100000000000",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": {"type": "binData", "subtype": 4}
    }
}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "4a6f686e20446f65",
                "to": "binData",
                "format": "hex"
            }
        },
        "to": {"type": "binData", "subtype": 130}
    }
}}');

SELECT * FROM bson_dollar_project('{"i": "SGVsbG8gd29ybGQh", "ts": 0 }', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "$i",
                "to": {"type": "binData", "subtype": "$ts"},
                "format": "utf8"
            }
        },
        "to": {"type": "binData", "subtype": "$ts"}
    }
}}');

SELECT * FROM bson_dollar_project('{"i": "167dee52-c331-484e-92d1-c56479b8e670"}', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "$i",
                "to": {"type": "binData", "subtype": 4},
                "format": "uuid"
            }
        },
        "to": {"type": "binData", "subtype": 4}
    }
}}');

SELECT * FROM bson_dollar_project('{"i": "SGVsbG8gd29ybGQh", "ts": 0 }', '{"result": {
    "$convert": {
        "input": {
            "$convert": {
                "input": "$i",
                "to": {"type": "binData", "subtype": "$ts"},
                "format": "utf8"
            }
        },
        "to": {"type": "binData", "subtype": 0}
    }
}}');

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
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "1", "value": {"$numberDouble": "1.3"}, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "2", "value": {"$numberDouble": "1.3"}, "convertTo": "array"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "3", "value": {"$numberDouble": "1.3"}, "convertTo": "object"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "4", "value": { "$oid" : "347f000000c1de008ec19ceb" }, "convertTo": "double"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "5", "value": { "$oid" : "347f000000c1de008ec19ceb" }, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "6", "value": { "$oid" : "347f000000c1de008ec19ceb" }, "convertTo": "long"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "7", "value": { "$oid" : "347f000000c1de008ec19ceb" }, "convertTo": "decimal"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "8", "value": true, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "9", "value": false, "convertTo": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "10", "value": {"$date": "2024-05-31T00:00:00.000Z"}, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "11", "value": {"$date": "2024-05-31T00:00:00.000Z"}, "convertTo": "int"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "12", "value": 1, "convertTo": "date"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "13", "value": 1.3, "convertTo": "minKey"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "14", "value": 1.3, "convertTo": "missing"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "15", "value": 1.3, "convertTo": "object"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "16", "value": 1.3, "convertTo": "array"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "17", "value": 1.3, "convertTo": "binData"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "18", "value": 1.3, "convertTo": "undefined"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "19", "value": 1.3, "convertTo": "null"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "20", "value": 1.3, "convertTo": "regex"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "21", "value": 1.3, "convertTo": "dbPointer"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "22", "value": 1.3, "convertTo": "javascript"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "23", "value": 1.3, "convertTo": "symbol"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "24", "value": 1.3, "convertTo": "javascriptWithScope"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "25", "value": 1.3, "convertTo": "timestamp"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "26", "value": 1.3, "convertTo": "maxKey"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "27", "value": 1, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "28", "value": {"$numberLong": "1"}, "convertTo": "objectId"}');
SELECT * from documentdb_api.insert_one('db', 'convertCollWithErrors', '{"_id": "29", "value": {"$numberDecimal": "1"}, "convertTo": "objectId"}');

SELECT bson_dollar_project(document, '{"output": {"$convert": { "input": "$value", "to": "$convertTo", "onError": "There was an error in $convert"}}}') FROM documentdb_api.collection('db', 'convertCollWithErrors');

-- $convert undefined/missing field used for onError should return empty result
SELECT bson_dollar_project(document, '{"output": {"$convert": { "input": "$value", "to": "$convertTo", "onError": "$missingField"}}}') FROM documentdb_api.collection('db', 'convertCollWithErrors');

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
WITH toQuery AS (SELECT bson_dollar_project(
  document,
  format(
    '{"expected": 1, "output": { "$%s": "$value" } }',
    CASE TRIM(BOTH '"' FROM document->>'convertTo')
      WHEN 'double'  THEN 'toDouble'
      WHEN 'string'  THEN 'toString'
      WHEN 'objectId' THEN 'toObjectId'
      WHEN 'bool'    THEN 'toBool'
      WHEN 'date'    THEN 'toDate'
      WHEN 'int'     THEN 'toInt'
      WHEN 'long'    THEN 'toLong'
      WHEN 'decimal' THEN 'toDecimal'
      ELSE 'toInvalid'  -- fail for easier diagnostics
    END
  )::bson
) as outDoc FROM documentdb_api.collection('db', 'convertColl'))
SELECT bson_dollar_project(outDoc, '{"result": {"$eq": ["$output", "$expected"]}}') FROM toQuery;

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
-- Currently double, decimal128, long, objectId, timestamp, date and string are supported.
-- Numeric, will be added to the Unix epoch 0 and shouldn't exceed int64 limits. Takes exactly 1 argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"input": ""}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": [[]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDouble": "9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDecimal": "9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDecimal": "-9223372036854775809"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": {"$numberDouble": "-9223372036854775809"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": 1}}');

-- $toDate test for string case
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2022-01-01T00:00:00.000Z"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2022-01-01 00:00:00.000"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2018-03-20T12:00:00+0500"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2018-03-20"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$toDate": "2017-Jul-04 noon"}}');

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

-- $convert with constant onError handling
-- Test invalid ObjectId conversion with onError (guaranteed to fail)
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": "xyz123", "to": "objectId", "onError": "CONVERSION_FAILED"}}}');

-- Test invalid number conversion with onError
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": "not_a_number_at_all", "to": "int", "onError": "ERROR_OCCURRED"}}}');

-- Test invalid date conversion with onError
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": "invalid_date_format", "to": "date", "onError": null}}}');

-- Test invalid boolean conversion with onError
SELECT * FROM bson_dollar_project('{}', '{"result": {"$convert": {"input": "maybe_true", "to": "bool", "onError": false}}}');