
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


SELECT object_id, document FROM documentdb_api.collection('db', 'nullfield') WHERE document @@ '{ "b": null}' ORDER BY object_id;

-- $in with miscellaneous datatypes 
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
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$in" : [ {"$regex": ".*y.*"}] } }';

-- test $nin
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ 1, 10 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ 1.0, 10 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ NaN, 1000 ] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ {"$numberInt": "2147483647"}, {"$numberLong": "9223372036854775807"},  {"$numberLong": "2147483646"}] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ {"$regex": ".*y.*"}] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ {"$regex": ".*a.*"}] } }';
SELECT document FROM documentdb_api.collection('db', 'queryoperatorIn') WHERE document @@ '{ "a" : { "$nin" : [ {"$regex": ".*a.*"}, {"$regex":"Lets.*"}, 1, 10] } }';

-- matches _id: 12 even though this condition should match none if applied per term.
SELECT document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt": 3, "$lt": 5 }}';
SELECT document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt": 5, "$gt": 3 }}';

-- this should evaluate to _id 10/12 even though it should evaluate to false
SELECT document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq": 2, "$lt": 1 }}';

-- test queries with NaN
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gte": NaN }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gt": NaN }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lte": NaN }}' ORDER BY object_id;
SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lt": NaN }}' ORDER BY object_id;

SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gte": NaN, "$lte": Infinity }}' ORDER BY object_id;


ROLLBACK;