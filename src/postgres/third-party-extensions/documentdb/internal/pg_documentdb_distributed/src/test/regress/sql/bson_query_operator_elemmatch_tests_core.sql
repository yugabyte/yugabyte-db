-- Test $elemMatch query operator

/* Insert with a being an array of elements*/
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 1, "a" : [ 1, 2 ] }', NULL);

/* Insert with a.b being an array of elements*/
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 2, "a" : { "b" : [ 10, 15, 18 ] } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 3, "a" : { "b" : [ 7, 18, 19 ] } }', NULL);

/* Insert with a being an array of objects*/
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 4, "a" : [ {"b" : 1 }, {"b" : 2 } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 5, "a" : [ {"b" : 3 }, {"b" : 2 } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 6, "a": [ {}, {"b": 2} ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 7, "a": [ {"b": 1, "c": 2}, {"b": 2, "c": 2} ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 8, "a": [ 1, 15, [18] ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 9, "a": [ 1, 15, {"b" : [18]} ] }', NULL);

/* Simple comparison */
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {} } }';

/* Comparison operator in elemMatch */
-- Comparison ops on same paths inside array
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": {"$gte" : 10, "$lte" : 15} }}';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$gte" : 1, "$lt" : 2} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$gt" : 1, "$lte" : 2} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$in": [3]} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$nin": [1, 2, 3]} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": null} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$eq" : [18]} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$eq" : 18} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$eq" : {"b" : [18]} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$eq" : {"b" : 18} } } }';

-- Comparison ops on different paths inside array
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": 1, "c": 2} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$ne": 1}, "c": 3} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": { "$gte": 1, "$lte": 1 }, "c": 2} } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$not": {"$elemMatch": {"b": 1, "c": 2} } } }';

SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$exists" : false} } } }';

SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$ne": 2}, "c": 2} } }';

/* Logical operator in elemMatch */
-- Logical ops on same path inside array
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$and": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$nor": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": { "$not": {"$gt" : 18, "$lte" : 19} } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$or": [{"b": "1"}, {"c": 2}] } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"$and" : [ { "a" : { "$elemMatch" : { "$gt" : 1, "$not" : { "$gt" : 2 } } } }]}';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"$or" : [ {"a.b": {"$elemMatch": {"$eq": 10}}}, {"a.b": {"$elemMatch": {"$eq": 7}}}]}';

-- elemMatch with Logical ops and non-logical op
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}], "b" : 3 } } }';

/* Insert with a being an array of objects and a.b is also an array*/
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 21, "a": [{ "b": [ 10, 15, 18 ], "d": [ {"e": 2} ] }] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 22, "a": [{ "b": [ 7, 18, 19 ], "d": [ {"e": 3} ], "f" : 1 }] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 23, "a": [{ "d": [ {"e": [2] } ] }] }', NULL);

/* Nested elemMatch */
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": { "$elemMatch" : { "$gte": 10, "$lt": 15 } } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"d": { "$elemMatch": { "e": { "$gte": 2, "$lte": 2 } } }, "b": { "$elemMatch": { "$gte": 10, "$lt": 55 } } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"d": { "$elemMatch": { "e": { "$gte": 2, "$in": [3] } } } } } }';

-- d.e being the path in nested elemMatch
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"d.e": { "$elemMatch": { "$gte": 2, "$lte": 2 } } } } }';

/* Non $elemMatch expression and a nested $elemMatch. */
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.f": 1, "a" : { "$elemMatch": {"d": { "$elemMatch": { "e": { "$gte": 2 } } } } } }';

/* Insert with a being an array of array or fieldPath contains number */
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 31, "a": [ [ 100, 200, 300 ] ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 32, "a": [ [ { "b" : 1 } ] ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 33, "a": [ [ { "b" : [1] } ] ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 34, "a": [ 100 ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 35, "a": { "0" : 100 } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 36, "a": { "0" : [ 100 ] } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 37, "a": [ { "0" : 100 } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 38, "a": [ { "0" : [ 100 ] } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 39, "a": [ { "-1" : 100 } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 40, "a": { "b" : [ [ 100, 200, 300 ] ] } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 41, "a": { "b" : [ { "c" : [100] } ] } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 42, "a": { "b" : [ { "c" : [[100]] } ] } }', NULL);


SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$elemMatch" : { "$in":[ 100 ] } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": { "$elemMatch" : { "$in":[ 100 ] } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$elemMatch" : { "b" : { "$eq" : 1 } } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "0" : 100 } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "0" : {"$gte" : 100 } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "-1" : {"$gte" : 100 } } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b.0" : { "$elemMatch": {"$eq": 100 } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b.0.c" : { "$elemMatch": {"$eq": 100 } } }';
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b.0.c.0" : { "$elemMatch": {"$eq": 100 } } }';

