
/* Insert with a.b being an object with various types*/
SELECT documentdb_api.drop_collection('db','queryoperator');
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 1, "a" : { "b" : 0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 2, "a" : { "b" : 1 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 3, "a" : { "b" : 2.0 }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 4, "a" : { "b" : "someString" }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 5, "a" : { "b" : true }}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": "ab_undefined", "a" : { "b" : {"$undefined": true }}}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": "ab_null", "a" : { "b" : null }}', NULL);

/* Insert elements to a root path for single path index $exists */
SELECT documentdb_api.drop_collection('db','singlepathindexexists');
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

-- Check NULL filter for @@ should not seg fault
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ NULL ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" :  2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" :  3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" :  [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" :  true }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" :  "c" }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : true }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : "c" }}' ORDER BY object_id;

/* array index equality */
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b.1": { "$eq" :  1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b.1": { "$ne" : 1 }}' ORDER BY object_id;

/* ensure documents match if there's equality on the field exactly */
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" :  { "b" : 1 } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" :  { "b" : [ 0, 1, 2 ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" :  [ { "b": 0 }, { "b": 1 }, { "b": 3 }] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" :  { "b" : [ { "1" : [1, 2, 3 ] } ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" :  1 }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" :  { "b" : 1 } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" :  { "b" : [ 0, 1, 2 ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" :  [ { "b": 0 }, { "b": 1 }, { "b": 3 }] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" :  { "b" : [ { "1" : [1, 2, 3 ] } ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" :  1 }}' ORDER BY object_id;

/* validation of query results for all operators */
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : [0, 0, 1] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : [true, false] }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : [0, 0, 1] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : [true, false] }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : [0, 0, 1] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : [true, false] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : null }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : [0, 0, 1] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : [true, false] }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : [0, 0, 1] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : [0, 1, 2] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : [true, false] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : null }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$exists" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$exists" : 0 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$exists" : "some" }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$exists" : [1, 2, 3 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : "int" }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : "string" }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : 3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : 3.0 }}' ORDER BY object_id;

/* Run some exists query against a collection with a single path index */
SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : 0 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : "some" }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : [1, 2, 3 ] }}' ORDER BY object_id;

SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 15, "c" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }', NULL);

-- These queries return the above document.
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c" : { "$eq" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c" : { "$eq" : { "d" : [ [ -1, 1, 2 ] ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0" : { "$eq" : { "d" : [ [ -1, 1, 2 ] ] } }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d" : { "$eq" : [ [ -1, 1, 2 ] ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d" : { "$eq" : [ -1, 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0" : { "$eq" : [ -1, 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0.0" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0.1" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0.2" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d" : { "$eq" : [ [ -1, 1, 2 ] ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d" : { "$eq" : [ -1, 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0" : { "$eq" : [ -1, 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0.0" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0.1" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0.2" : { "$eq" : 2 }}' ORDER BY object_id;

-- these queries do not return the above document
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.1" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.2" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.0.d.0.2" : { "$eq" : [ 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0" : { "$eq" : -1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.0" : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.1" : { "$eq" : 1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db','queryoperator') WHERE document @@ '{ "c.d.2" : { "$eq" : 2 }}' ORDER BY object_id;

-- queries on the _id field
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$ne" : null }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$in": [ 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$gt": 20, "$lt": 40 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$all": [ 2 ], "$in": [ 1, 2 ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$all": [ 2 ], "$ne": 40 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$all" : [ 1, 2 ] }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$gt" : 1, "$lte" : 5 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1},{"_id":1}]}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1},{"_id":2}]}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or"  : [{"_id":1},{"_id":2}]}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or"  : [{"_id": { "$gt": 1, "$lte" : 5 }}, {"_id":1}]}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1}, {"_id": { "$gte": 1, "$lte" : 5 }}]}' ORDER BY object_id;

-- $all with $elemMatch queries
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 16, "a" : [{ "b" : {} }, { "b" : 0 }]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 17, "a" : []}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 18, "a" : [{ "b" : "S", "c": 10, "d" : "X"}, { "b" : "M", "c": 100, "d" : "X"}, { "b" : "L", "c": 100, "d" : "Y"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 19, "a" : [{ "b" : "1", "c": 100, "d" : "Y"}, { "b" : "2", "c": 50, "d" : "X"}, { "b" : "3", "c": 100, "d" : "Z"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 20, "a" : [{ "b" : "M", "c": 100, "d" : "Y"}]}', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 21, "a": [{ "b": [ { "c" : 10 }, { "c" : 15 }, {"c" : 18 } ], "d" : [ { "e" : 10 }, { "e" : 15 }, {"e" : 18 } ]}] }', NULL);
SELECT documentdb_api.insert_one('db','queryoperator', '{"_id": 22, "a": [{ "b": [ { "c" : 11 } ], "d" : [ { "e" : 20 }, { "e" : 25 } ]}] }', NULL);

-- test specific scenarios for array of elements such as empty array, array of array
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b"  : {"$all" : [0, 1]}}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [[]] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b"  : {"$all" : [[-1, 1, 2]] } }' ORDER BY object_id;

-- test empty document expression in $all
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b"  : {"$all" : [ {} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b"  : {"$all" : [ 0, {} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b"  : {"$all" : [ {}, 0 ] } }' ORDER BY object_id;

-- test non-elemMatch expression document in $all
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [{ "b" : 0 }] } }' ORDER BY object_id;

-- test $elemMatch inside $all
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a" : {"$all" : [{"$elemMatch" : {"b" : {"$gt" : 1} } }]}}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a" : {"$all" : [{"$elemMatch" : {"b" : "M", "c" : {"$gt" : 50} } }, {"$elemMatch" : {"c" : 100, "d" : "Y" } }]}}' ORDER BY object_id;

-- test nested $elemMatch inside $all
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') where document @@ '{"a" : {"$all" : [{"$elemMatch" : {"b" : {"$elemMatch": {"c" : {"$gt" : 1} } } } }] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') where document @@ '{"a" : {"$all" : [{"$elemMatch" : {"b" : {"$elemMatch": {"c" : {"$gt" : 1} } }, "d" : {"$elemMatch": {"e" : {"$lt" : 20} } } } }] } }';

SELECT documentdb_api.drop_collection('db','nullfield');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 1, "a" : 1, "b": 1}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 2, "a" : 2, "b": null}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 3, "a" : 3}');

SELECT 1 FROM documentdb_api.shard_collection('db','nullfield', '{"b": "hashed"}', false);

SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 4, "a" : 10, "b": 1}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 5, "a" : 20, "b": null}');
SELECT 1 FROM documentdb_api.insert_one('db','nullfield', '{"_id": 6, "a" : 30}');

SELECT object_id, document FROM documentdb_api.collection('db', 'nullfield') WHERE document @@ '{ "b": null}' ORDER BY object_id;

-- error cases below this point.
-- run outside the parent transaction.
ROLLBACK;

-- these queries are negative tests for $size operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : -3 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : 3.1 }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$size" : -3.4 }}' ORDER BY object_id;

-- These can't be tested since the extended json syntax treats $type as an extended json operator and not a call to actual $type function.
-- SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : 123.56 }}' ORDER BY object_id;
-- SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$type" : [] }}' ORDER BY object_id;

-- these queries are negative tests for $all operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, 1 ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ 1, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, {"$all" : [0]} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$all" : [0]}, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, {} ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {}, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, { "b" : 1 } ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ { "b" : 1 }, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$elemMatch" : {"b" : {"$gt" : 1} } }, { "$or" : [ {"b":1} ] } ] } }' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ { "$or" : [ {"b":1} ] }, {"$elemMatch" : {"b" : {"$gt" : 1} } } ] } }' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [ {"$all" : [1] } ] } }' ORDER BY object_id;

-- negative tests for $not operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$not" : {} } }';

-- negative tests for comp operators with regex
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$ne" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$gt" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$gte" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$lt" : {"$regex" : "hello", "$options" : ""} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$lte" : {"$regex" : "hello", "$options" : ""} } }';

-- type undefined, no-operator
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : {"$undefined" : true } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : 1, "b" : { "$undefined" : true } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b" : { "$undefined" : true } }';

-- type undefined, comparison query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$eq" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$ne" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$gt" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$gte" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$lt" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$lte" : { "$undefined" : true } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in" : [ { "$undefined" : true }, 2, 3, 4 ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin" : [ { "$undefined" : true }, 6, 7, 8] } }';

-- type undefined, array query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$all" : [ { "$undefined" : true } ] } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$all" : [ { "$undefined" : true }, 2 ] } }';

--type undefined, bitwise query operators
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllClear": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllClear": [1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllSet": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAllSet":[1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnyClear": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnyClear": [1, {"$undefined": true}]}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnySet": {"$undefined": true}}}';
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{"a": {"$bitsAnySet": [1, {"$undefined": true}]}}';

-- $in with miscellaneous datatypes 
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

SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ 1, 10 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ 1.0, 10 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ 1.01, 10 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [  "first value", {"$numberDouble": "1.000000000000001"} ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [  "first value", {"$numberDecimal": "1.000000000000000000000000000000001"} ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ {"b":1},[1,2,3,4,5,6], "Lets Optimize dollar In"  ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ null, 1 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ null, {"$maxKey" : 1}, {"$minKey" : 1} ] }}';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ {"$binary": { "base64": "ww==", "subType": "01"}}, {"$date": "2019-01-30T07:30:10.136Z"}, {"$oid": "639926cee6bda3127f153bf1"} ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ NaN, 1000 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ Infinity, 1000  ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ 0, 1000  ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ {"$numberDecimal": "0.0"}, 1000  ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ {"$numberInt": "2147483647"}, {"$numberLong": "9223372036854775807"},  {"$numberLong": "2147483646"}] } }';
SELECT documentdb_api.drop_collection('db','queryoperatorIn');
