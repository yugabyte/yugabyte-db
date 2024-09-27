SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 6630000;
SET helio_api.next_collection_id TO 6630;
SET helio_api.next_collection_index_id TO 6630;

SELECT helio_api.insert_one('db', 'distinct1', '{ "a": 1, "b": { "c": "foo" } }');
SELECT helio_api.insert_one('db', 'distinct1', '{ "a": 2, "b": { "c": "bar" } }');
SELECT helio_api.insert_one('db', 'distinct1', '{ "a": 2, "b": { "c": "baz" } }');
SELECT helio_api.insert_one('db', 'distinct1', '{ "a": 2, "b": { "c": "foo" } }');
SELECT helio_api.insert_one('db', 'distinct1', '{ "a": 3, "b": { "c": "foo" } }');

-- this is what the query will look like from the GW
PREPARE distinctQuery(text, text, text) AS (WITH r1 AS (SELECT DISTINCT 
    bson_distinct_unwind(document, $3) AS document FROM helio_api.collection($1, $2)) 
    SELECT bson_build_distinct_response(COALESCE(array_agg(document), '{}'::bson[])) FROM r1);

PREPARE distinctQueryWithFilter(text, text, text, bson) AS (WITH r1 AS (SELECT DISTINCT 
    bson_distinct_unwind(document, $3) AS document FROM helio_api.collection($1, $2) WHERE document @@ $4 )
    SELECT bson_build_distinct_response(COALESCE(array_agg(document), '{}'::bson[])) FROM r1);

EXECUTE distinctQuery('db', 'distinct1', 'a');
EXECUTE distinctQueryWithFilter('db', 'distinct1', 'a', '{ "a": { "$lt": 3 }}');

EXECUTE distinctQuery('db', 'distinct1', 'b.c');

SELECT helio_api.insert_one('db', 'distinct2', '{ "a": null }');
EXECUTE distinctQuery('db', 'distinct2', 'a.b');

SELECT helio_api.insert_one('db', 'distinct2', '{ "b": 1 }');
SELECT helio_api.insert_one('db', 'distinct2', '{ }');
EXECUTE distinctQuery('db', 'distinct2', 'b');


SELECT helio_api.insert_one('db', 'distinct3', '{ "a": [ 1, 2, 3 ] }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "a": [ 2, 3, 4 ] }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "a": [ 3, 4, 5 ] }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "a": 9 }');

EXECUTE distinctQuery('db', 'distinct3', 'a');
EXECUTE distinctQuery('db', 'distinct3', 'a.0');
EXECUTE distinctQuery('db', 'distinct3', 'a.1');

SELECT helio_api.insert_one('db', 'distinct3', '{ "e": [ { "b": "a" }, { "b": "d"} ], "c": 12 }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "e": [ { "b": "b" }, { "b": "d"} ], "c": 12 }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "e": [ { "b": "c" }, { "b": "e"} ], "c": 12 }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "e": [ { "b": "c" }, { "b": "f"} ], "c": 12 }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "e": [  ], "c": 12 }');
SELECT helio_api.insert_one('db', 'distinct3', '{ "e": { "b": "z" }, "c": 12 }');

EXECUTE distinctQuery('db', 'distinct3', 'e.b');

EXECUTE distinctQuery('db', 'distinct3', 'e.0.b');
EXECUTE distinctQuery('db', 'distinct3', 'e.1.b');

EXECUTE distinctQuery('db', 'distinct3', 'e');

EXECUTE distinctQueryWithFilter('db', 'distinct3', 'e.b', '{ "e.b": { "$gt": "d" } }');


SELECT helio_api.insert_one('db', 'distinct4', '{ "a": { "b": { "c": 1 } } }');
SELECT helio_api.insert_one('db', 'distinct4', '{ "a": { "b": { "c": 2 } } }');
SELECT helio_api.insert_one('db', 'distinct4', '{ "a": { "b": { "c": 3 } } }');
SELECT helio_api.insert_one('db', 'distinct4', '{ "a": { "b": { "notRelevant": 3 } } }');
SELECT helio_api.insert_one('db', 'distinct4', '{ "a": { "notRelevant": 3 } }');

EXECUTE distinctQueryWithFilter('db', 'distinct4', 'a.b.c', '{ "a.b.c": { "$gt": 0 } }');
EXECUTE distinctQueryWithFilter('db', 'distinct4', 'a.b.c', '{ "a.b.c": { "$gt": 1 } }');

-- test for DBRef
SELECT helio_api.insert_one('db', 'distinct5', '{ "a": 1, "b": { "$ref" : "distinct6", "$id" : { "$oid" : "147f000000c1de008ec19ceb" }}}');
select helio_api.insert_one('db', 'distinct5', '{ "a": 2, "b": { "$ref" : "distinct6", "$id" : { "$oid" : "147f000000c1de008ec19cea" }}}');
select helio_api.insert_one('db', 'distinct5', '{ "a": 3, "b": { "$ref" : "distinct6", "$id" : { "$oid" : "147f000000c1de008ec19cea" }}}');
select helio_api.insert_one('db', 'distinct6', '{ "_id": { "$oid" : "147f000000c1de008ec19cea" }, "c": 1}');
select helio_api.insert_one('db', 'distinct6', '{ "_id": { "$oid" : "147f000000c1de008ec19ceb" }, "c": 2}');

EXECUTE distinctQueryWithFilter('db', 'distinct5', 'a', '{ "b": { "$ref": "distinct6", "$id": { "$oid" : "147f000000c1de008ec19ceb" } } }');
EXECUTE distinctQueryWithFilter('db', 'distinct5', 'a', '{ "b": { "$ref": "distinct6", "$id": { "$oid" : "147f000000c1de008ec19cea" } } }');

-- negative tests
EXECUTE distinctQueryWithFilter('db', 'distinct5', 'a', '{ "b": { "$id": { "$oid" : "147f000000c1de008ec19cea" }, "$ref": "distinct6" } }');
EXECUTE distinctQueryWithFilter('db', 'distinct5', 'a', '{ "b": { "$ref": "distinct6", "$id": { "$oid" : "147f000000c1de008ec19cea" }, "$un": "t" } }');

-- optional parameter - $db
SELECT helio_api.insert_one('db2', 'distinct7', '{ "d": 20, "b": { "$ref" : "distinct6", "$id" : { "$oid" : "147f000000c1de008ec19ceb" }, "$db": "db" }}');
SELECT helio_api.insert_one('db2', 'distinct7', '{ "d": 30, "b": { "$ref" : "distinct6", "$id" : { "$oid" : "147f000000c1de008ec19ceb" }}}');
-- expect to get 20
EXECUTE distinctQueryWithFilter('db2', 'distinct7', 'd', '{ "b": { "$ref": "distinct6", "$id": { "$oid" : "147f000000c1de008ec19ceb" }, "$db": "db" } }');
-- expect to get 30
EXECUTE distinctQueryWithFilter('db2', 'distinct7', 'd', '{ "b": { "$ref": "distinct6", "$id": { "$oid" : "147f000000c1de008ec19ceb" } } }');
