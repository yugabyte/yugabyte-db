
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 1, "a" : { "b" : 0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 2, "a" : { "b" : 1 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 3, "a" : { "b" : 2.0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 4, "a" : { "b" : "someString" }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 5, "a" : { "b" : true }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": "ab_undefined", "a" : { "b" : {"$undefined": true }}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": "ab_null", "a" : { "b" : null }}', NULL);

/* Insert elements to a root path for single path index $exists */
SELECT documentdb_api.insert_one('db','singlepathindexexists', '{"_id": 1, "a" : 0}', NULL);
SELECT documentdb_api.insert_one('db','singlepathindexexists', '{"_id": 4, "a" : { "b" : "someString" }}', NULL);
SELECT documentdb_api.insert_one('db','singlepathindexexists', '{"_id": "b", "b" : 1}', NULL);
SELECT documentdb_api.insert_one('db','singlepathindexexists', '{"_id": "a", "a": {"$undefined": true }}', NULL);
SELECT documentdb_api.insert_one('db','singlepathindexexists', '{"_id": "a_null", "a" : null}', NULL);

/* insert some documents with a.{some other paths} */
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 6, "a" : 1}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 7, "a" : true}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 8, "a" : [0, 1, 2]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 9, "a" : { "c": 1 }}', NULL);

/* insert paths with nested objects arrays */
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', NULL);

SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 1, "a" : 1}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 2, "a" : {"$numberDecimal": "1.0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 3, "a" : {"$numberDouble": "1.0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 4, "a" : {"$numberLong": "1"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 5, "a" : {"$numberDecimal": "1.0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 6, "a" : {"$numberDouble": "1.0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 7, "a" : {"$numberDecimal": "1.000000000000000000000000000000001"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 8, "a" : {"$numberDouble": "1.000000000000001"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 9, "a" : {"$binary": { "base64": "ww==", "subType": "01"}}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 10, "a" : {"$binary": { "base64": "ww==", "subType": "02"}}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 11, "a" : {"$binary": { "base64": "zg==", "subType": "01"}}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 12, "a" : {"$binary": { "base64": "zg==", "subType": "02"}}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 13, "a" : {"$timestamp" : { "t": 1670981326, "i": 1 }}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 14, "a" : {"$date": "2019-01-30T07:30:10.136Z"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 15, "a" : {"$oid": "639926cee6bda3127f153bf1"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 18, "a" : {"$maxKey" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 19, "a" : {"$minKey" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 20, "a" : { "$undefined" : true }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 21, "a" : null}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 22, "a" : {"b":1}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 23, "a" : {"b":2}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 24, "a" : [1,2,3,4,5]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 25, "a" : [1,2,3,4,5,6]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 26, "a" : "Lets Optimize dollar In"}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 27, "a" : "Lets Optimize dollar In again"}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 28, "a" : NaN}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 29, "a" : [1,2,3,NaN,4]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 30, "a" : Infinity}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 31, "a" : [1,2,3,Infinity,4]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 32, "a" : {"$numberDouble": "0.0000"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 33, "a" : {"$numberDecimal": "0.0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 34, "a" : {"$numberLong": "0"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 35, "a" : 0}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 36, "a" : {"$numberLong": "9223372036854775807"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 37, "a" : {"$numberLong": "9223372036854775806"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 38, "a" : {"$numberInt": "2147483647"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 39, "a" : {"$numberInt": "2147483646"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 40, "a" : {"$numberInt": "2147483645"}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperatorIn', '{"_id": 41, "a" : ["abc", "xyz1"]}', NULL);

SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 1, "a" : 1, "b": 1}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 2, "a" : 2, "b": null}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 3, "a" : 3}');

SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 4, "a" : 10, "b": 1}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 5, "a" : 20, "b": null}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 6, "a" : 30}');

-- $all with $elemMatch queries
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 16, "a" : [{ "b" : {} }, { "b" : 0 }]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 17, "a" : []}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 18, "a" : [{ "b" : "S", "c": 10, "d" : "X"}, { "b" : "M", "c": 100, "d" : "X"}, { "b" : "L", "c": 100, "d" : "Y"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 19, "a" : [{ "b" : "1", "c": 100, "d" : "Y"}, { "b" : "2", "c": 50, "d" : "X"}, { "b" : "3", "c": 100, "d" : "Z"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 20, "a" : [{ "b" : "M", "c": 100, "d" : "Y"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 21, "a": [{ "b": [ { "c" : 10 }, { "c" : 15 }, {"c" : 18 } ], "d" : [ { "e" : 10 }, { "e" : 15 }, {"e" : 18 } ]}] }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 22, "a": [{ "b": [ { "c" : 11 } ], "d" : [ { "e" : 20 }, { "e" : 25 } ]}] }', NULL);

/* insert NaN and Infinity */
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 23, "a" : NaN}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 24, "a" : Infinity}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 25, "a" : -Infinity}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 26, "a" : [NaN, Infinity]}', NULL);