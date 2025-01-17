
/* Insert with a.b being an object with various types*/
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 1, "a" : { "b" : 0 }}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 2, "a" : { "b" : 1 }}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 3, "a" : { "b" : 2.0 }}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 4, "a" : { "b" : "someString" }}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 5, "a" : { "b" : true }}', NULL);

/* insert some documents with a.{some other paths} */
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 6, "a" : 1}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 7, "a" : true}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 8, "a" : [0, 1, 2]}', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 9, "a" : { "c": 1 }}', NULL);

/* insert paths with nested objects arrays */
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', NULL);

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": true }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": "c" }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": true }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": "c" }';

/* array index equality */
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b.1": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b.1": 1 }';

/* ensure documents match if there's equality on the field exactly */
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a" : { "b" : 1 } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a" : { "b" : [ 0, 1, 2 ] } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3 }] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a" : 1 }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a" : { "b" : 1 } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a" : { "b" : [ 0, 1, 2 ] } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3 }] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a" : 1 }';

/* validation of query results for all operators */
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": [0, 0, 1] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": [true, false] }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": [0, 0, 1] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": [true, false] }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": [0, 0, 1] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": [true, false] }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": [0, 0, 1] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": [true, false] }';

SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": 1 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": 2 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": 3 }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": [0, 0, 1] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": [0, 1, 2] }';
SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": [true, false] }';

SELECT helio_api.insert_one('db','querydollartest', '{"_id": 15, "c" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }', NULL);

-- These queries return the above document.
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c" : { "d" : [ [ -1, 1, 2 ] ] } }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0" : { "d" : [ [ -1, 1, 2 ] ] } }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d" : [ [ -1, 1, 2 ] ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d" : [ -1, 1, 2 ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0" : [ -1, 1, 2 ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0.0" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0.1" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0.2" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d" : [ [ -1, 1, 2 ] ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d" : [ -1, 1, 2 ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0" : [ -1, 1, 2 ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0.0" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0.1" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0.2" : 2 }';

-- these queries do not return the above document
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.1" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.2" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.0.d.0.2" : [ 2 ] }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0" : -1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.0" : 2 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.1" : 1 }';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @= '{ "c.d.2" : 2 }';

SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a": 1 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a": 0 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.b" : 1 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.b" : 0 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.0": 1 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.0": 0 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.b.0": 1}';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "a.b.0": 0}';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "c.0.d": 1}';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "c.0.d": 0}';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "c.d.0": 1}';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @? '{ "c.d.0": 0}';


SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a.b": "string" }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a.b": "int" }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a.b": 16 }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a.b": [ "int", "string" ] }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a.b": [ 16, "string" ] }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a": "array" }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @# '{ "a": [ "array", "object" ] }';
SELECT COUNT(*) FROM helio_api.collection('db','querydollartest') WHERE document @@ '{ "a": { "$type": "number" } }';

SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @*= '{ "a.b": [ 1, "c", [0, 1, 2] ]}';
SELECT object_id, document FROM helio_api.collection('db','querydollartest') WHERE document @!*= '{ "a.b": [ 1, "c", [0, 1, 2] ]}';

-- Test double type ordering (see nan.js)
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 16, "e" : {"$numberDouble": "-Infinity" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 17, "e" : {"$numberDouble": "-3" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 18, "e" : {"$numberDouble": "0" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 19, "e" : {"$numberDouble": "3" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 20, "e" : {"$numberDouble": "Infinity" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 21, "e" : {"$numberDouble": "NaN" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 22, "e" : {"$numberDouble": "-NaN" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 23, "e" : null }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 24, "e" : [] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 25, "e" : { "c": 1 } }', NULL);

SELECT document -> 'e' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "e": { "$numberDouble": "NaN" } }';
SELECT document -> 'e' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "e": { "$lt" : { "$numberDouble": "NaN" } } }';
SELECT document -> 'e' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "e": { "$lte" : { "$numberDouble": "NaN" } } }';
SELECT document -> 'e' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "e": { "$gt" : { "$numberDouble": "NaN" } } }';
SELECT document -> 'e' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "e": { "$gte" : { "$numberDouble": "NaN" } } }';


-- Test for nulls (null.js)
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 26, "f" : 1 }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 27, "f" : null }', NULL);

SELECT COUNT(*) FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "f": null, "_id": { "$gte": 26 } }';
SELECT COUNT(*) FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "f": { "$ne" : null }, "_id": { "$gte": 26 } }';
SELECT COUNT(*) FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "g": { "$eq" : null }, "_id": { "$gte": 26 } }';
SELECT COUNT(*) FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "g": { "$ne" : null }, "_id": { "$gte": 26 } }';

-- Test for nulls (null2.js)
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 28, "h" : [ { "b": 5 }] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 29, "h" : [ { }] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 30, "h" : [ ] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 31, "h" : [{}, { "b": 5 } ] }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 32, "h" : [5, { "b": 5 } ] }', NULL);

SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "h.b": null, "_id": { "$gte": 28 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "h.b": { "$in" : [ null ] }, "_id": { "$gte": 28 }  }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "h.b": { "$ne": null }, "_id": { "$gte": 28 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "h.b": { "$nin" : [ null ] }, "_id": { "$gte": 28 }  }';


SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 33, "j" : { "$numberInt" : "1" } }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 34 }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 35, "j" : null }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 36, "j" : { "k" : { "$numberInt" : "1" }} }', NULL);
SELECT helio_api.insert_one('db','querydollartest', '{ "_id": 37, "j" : { "k" : { "$numberInt" : "2" }} }', NULL);

SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "j.k": null }' AND document @>= '{ "_id": 33 }';


-- Test for nulls/undefined
SELECT helio_api.insert_one('db', 'querydollartest', '{ "_id": 38, "l": null }');
SELECT helio_api.insert_one('db', 'querydollartest', '{ "_id": 39, "l": {"$undefined": true} }');
SELECT helio_api.insert_one('db', 'querydollartest', '{ "_id": 40 }');
SELECT helio_api.insert_one('db', 'querydollartest', '{ "_id": 41, "l": 1 }');

SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "l": null, "_id": { "$gte": 38 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "l": { "$in": [ null, 5 ] }, "_id": { "$gte": 38 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "l": { "$all": [ null ] }, "_id": { "$gte": 38 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "l": { "$ne": null }, "_id": { "$gte": 38 } }';
SELECT document-> '_id' FROM helio_api.collection('db', 'querydollartest') WHERE document @@ '{ "l": { "$nin": [ null, 5 ] }, "_id": { "$gte": 38 } }';