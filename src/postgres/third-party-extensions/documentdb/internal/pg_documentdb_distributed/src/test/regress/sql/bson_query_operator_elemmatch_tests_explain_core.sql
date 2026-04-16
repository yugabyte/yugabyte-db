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

/* Insert with a being an array of objects and a.b is also an array*/
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 10, "a": [{ "b": [ 10, 15, 18 ], "d": [ {"e": 2} ] }] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 12, "a": [{ "b": [ 7, 18, 19 ], "d": [ {"e": 3} ], "f" : 1 }] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 13, "a": [{ "d": [ {"e": [2] } ] }] }', NULL);

-- run an explain analyze
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": {"$gte" : 10, "$lte" : 15} }}';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$in": [3]} } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$nin": [1, 2, 3]} } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"$eq" : {"b" : [18]} } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": {"$exists" : false} } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": 1, "c": 2} } }';

EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$and": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$nor": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": { "$not": {"$gt" : 18, "$lte" : 19} } } }';

-- elemMatch with Logical ops and non-logical op
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}], "b" : 3 } } }';
/* Nested elemMatch */
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"b": { "$elemMatch" : { "$gte": 10, "$lt": 15 } } } } }';

-- NOTE: The explain plan can be confusin while printing for alias name for two adjacent elemMatch. In SubPlan 2, you will see it has alias elemmatchd2_1 created but using elemmatchd2 in filters. As per the query plan, filter qual is using attNumber=1 which is expected to refer its immediate RTE that will have :colnames ("elemmatchd2"). I have also tested using query: it is not possible that alias defined inside one EXISTS() be used outside of it and inside any other EXISTS().
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": {"d": { "$elemMatch": { "e": { "$gte": 2, "$lte": 2 } } }, "b": { "$elemMatch": { "$gte": 10, "$lt": 55 } } } } }';

/* Non $elemMatch expression and a nested $elemMatch. */
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.f": 1, "a" : { "$elemMatch": {"d": { "$elemMatch": { "e": { "$gte": 2 } } } } } }';

/* Insert with a being an array of array or fieldPath contains number */
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 31, "a": [ [ 100, 200, 300 ] ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 32, "a": [ 100 ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 33, "a": { "0" : 100 } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 34, "a": { "0" : [ 100 ] } }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 35, "a": [ { "0" : 100 } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 36, "a": [ { "0" : [ 100 ] } ] }', NULL);
SELECT documentdb_api.insert_one('db','elemmatchtest', '{"_id": 37, "a": [ { "-1" : 100 } ] }', NULL);

-- below queries will use $type:array internally as first filter
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$elemMatch" : { "$in":[ 100 ] } } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "0" : 100 } } }';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "0" : {"$gte" : 100 } } } }';
-- below query will not use $type internally
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "-1" : {"$gte" : 100 } } } }';
