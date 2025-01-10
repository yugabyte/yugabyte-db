
SET search_path TO helio_api,helio_core;
SET helio_api.next_collection_id TO 1000;
SET helio_api.next_collection_index_id TO 1000;

-- insert int32
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"1", "value": { "$numberInt" : "11" }, "valueMax": { "$numberInt" : "2147483647" }, "valueMin": { "$numberInt" : "-2147483648" }}', NULL);

-- insert int64
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"2", "value":{"$numberLong" : "134311"}, "valueMax": { "$numberLong" : "9223372036854775807" }, "valueMin": { "$numberLong" : "-9223372036854775808" }}', NULL);

-- insert double
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"3", "value":{"$numberDouble" : "0"}, "valueMax": { "$numberDouble" : "1.7976931348623157E+308" }, "valueMin": { "$numberDouble" : "-1.7976931348623157E+308" }, "valueEpsilon": { "$numberDouble": "4.94065645841247E-324"}, "valueinfinity": {"$numberDouble":"Infinity"}}', NULL);

-- insert string
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"4", "value": "A quick brown fox jumps over the lazy dog."}', NULL);

-- insert binary
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"5", "value": {"$binary": { "base64": "U29tZVRleHRUb0VuY29kZQ==", "subType": "02"}}}', NULL);

-- minKey/maxKey
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"6", "valueMin": { "$minKey": 1 }, "valueMax": { "$maxKey": 1 }}', NULL);

-- oid, date, time
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"7", "tsField": {"$timestamp":{"t":1565545664,"i":1}}, "dateBefore1970": {"$date":{"$numberLong":"-1577923200000"}}, "dateField": {"$date":{"$numberLong":"1565546054692"}}, "oidField": {"$oid":"5d505646cf6d4fe581014ab2"}}', NULL);

-- array & nested object
SELECT helio_api.insert_one('db','bsontypetests','{"_id":"8", "arrayOfObject": [ { "ola": "ola"}, { "tudo bem?": "tudo bem!"}, { "o que tu fizeste essa semana?" : "nada" } ]}', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM helio_data.documents_1001 ORDER BY 1,2,3;

-- project two fields out.
SELECT document->'_id', document->'value' FROM helio_data.documents_1001 ORDER BY object_id;

-- insert document with $ or . in the field path
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 9, "$field": 1}');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 10, "field": { "$subField": 1 } }');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 11, "field": [ { "$subField": 1 } ] }');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 12, ".field": 1}');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 13, "fie.ld": 1}');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 14, "field": { ".subField": 1 } }');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 15, "field": { "sub.Field": 1 } }');
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": 16, "field": [ { "sub.Field": 1 } ] }');

/* Test to validate that _id field cannot have regex as it's value */
select helio_api.insert_one('db', 'bsontypetests', '{"_id": {"$regex": "^A", "$options": ""}}');

/* Test _id cannot have nested paths with $ */
SELECT helio_api.insert_one('db', 'bsontypetests', '{ "_id": { "a": 2, "$c": 3 } }');

/* Test to validate that _id field cannot have array as it's value */
select helio_api.insert_one('db', 'bsontypetests', '{"_id": [1]}');

-- assert object_id matches the '_id' from the content - should be numRows.
SELECT COUNT(*) FROM helio_data.documents_1001 where object_id::bson = bson_get_value(document, '_id');
