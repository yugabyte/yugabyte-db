/* validate explain */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$eq" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$ne" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gt" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$gte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lt" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a.b": { "$lte" : 1 }}';

/* validate explain for single path index */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$lte" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : 1 }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'singlepathindexexists') WHERE document @@ '{ "a": { "$exists" : 0 }}';

/* validate explain for the individual items as well */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gt" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gt" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gt" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gte" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gte" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$gte" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lt" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lt" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lt" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lte" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lte" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$lte" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$eq" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$eq" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$eq" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$ne" : { "b": 1 } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$ne" : { "b": [ true, false ] } }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a": { "$ne" : [ { "b": [ 2, 3, 4 ] } ] }}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2 }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$ne" : null }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$in": [ 1, 2 ] }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$gt": 20, "$lt": 40 }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$all": [ 2 ], "$in": [ 1, 2 ] } }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$all": [ 2 ], "$ne": 40 }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$eq" : 2, "$all" : [ 1, 2 ] }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "_id"  : { "$gt" : 1, "$lte" : 5 }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1},{"_id":1}]}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1},{"_id":2}]}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or"  : [{"_id":1},{"_id":2}]}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or"  : [{"_id": { "$gt": 1, "$lte" : 5 }}, {"_id":1}]}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [{"_id":1}, {"_id": { "$gte": 1, "$lte" : 5 }}]}' ORDER BY object_id;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a"  : {"$all" : [{"$elemMatch" : {"b" : "M", "c" : {"$gt" : 50} } }, {"$elemMatch" : {"c" : 100, "d" : "Y" } }]}}' ORDER BY object_id;

-- $or gets converted to $in for simple expressions.
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": 3 }, { "b": 4 } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": 3 }, { "a": { "$gte": 4 } } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": 3 }, { "a": 5 } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": 3 }, { "$and": [ { "a": 5 } ] } ] }' ORDER BY object_id;

-- $and/$or with a single element gets elided
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": { "$gt": 3 } } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [ { "a": { "$gt": 3 } } ] }' ORDER BY object_id;

-- $or of $exists false/$eq: null simplifies
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [ { "a": 3 }, { "$or": [ { "b": null }, { "b": { "$exists": false }} ] } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [ { "a": 3 }, { "$or": [ { "b": null }, { "b": { "$exists": true }} ] } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [ { "a": 3 }, { "$or": [ { "b": null }, { "c": { "$exists": false }} ] } ] }' ORDER BY object_id;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$and" : [ { "a": 3 }, { "$or": [ { "b": null }, { "c": null }, { "c": { "$exists": false }} ] } ] }' ORDER BY object_id;

-- $in with 1 element gets converted to $eq
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in": [ 1 ]} }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in": [ ]} }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$in": [ 1, 2 ]} }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin": [ 1 ]} }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin": [ ]} }' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "a" : { "$nin": [ 1, 2 ]} }' ORDER BY object_id;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'queryoperator') WHERE document @@ '{ "$or" : [ { "a": 3 }, { "a": { "$in": [ 4 ] }} ] }' ORDER BY object_id;