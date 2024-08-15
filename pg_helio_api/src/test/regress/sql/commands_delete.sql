SET search_path TO helio_api,helio_core,helio_api_catalog;
SET helio_api.next_collection_id TO 2500;
SET helio_api.next_collection_index_id TO 2500;

-- Call delete for a non existent collection.
-- Note that this should not report any logs related to collection catalog lookup.
SET citus.log_remote_commands TO ON;
SELECT helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"a":5},{"a":{"$gt":0}}]},"limit":0}]}');
RESET citus.log_remote_commands;

select 1 from helio_api.insert_one('db', 'removeme', '{"a":1,"_id":1}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":2,"_id":2}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":3,"_id":3}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":4,"_id":4}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":5,"_id":5}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":6,"_id":6}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":7,"_id":7}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":8,"_id":8}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":9,"_id":9}');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":10,"_id":10}');

-- exercise invalid delete syntax errors
select helio_api.delete('db', NULL);
select helio_api.delete(NULL, '{"delete":"removeme", "deletes":[{"q":{},"limit":0}]}');
select helio_api.delete('db', '{"deletes":[{"q":{},"limit":0}]}');
select helio_api.delete('db', '{"delete":"removeme"}');
select helio_api.delete('db', '{"delete":["removeme"], "deletes":[{"q":{},"limit":0}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":{"q":{},"limit":0}}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":0}], "extra":1}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{}}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"limit":0}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":[],"limit":0}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":0,"extra":1}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":0}],"ordered":1}');

-- Disallow writes to system.views
select helio_api.delete('db', '{"delete":"system.views", "deletes":[{"q":{},"limit":0}]}');

-- delete all
begin;
SET LOCAL search_path TO '';
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":0}]}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete some
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$lte":3}},"limit":0}]}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- arbitrary limit type works in Mongo
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$lte":3}},"limit":{"hello":"world"}}]}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete all from non-existent collection
select helio_api.delete('db', '{"delete":"notexists", "deletes":[{"q":{},"limit":0}]}');

-- query syntax errors are added the response
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$ltr":5}},"limit":0}]}');

-- when ordered, expect only first delete to be executed
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":1},"limit":0},{"q":{"$a":2},"limit":0},{"q":{"a":3},"limit":0}]}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":1},"limit":0},{"q":{"$a":2},"limit":0},{"q":{"a":3},"limit":0}],"ordered":true}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- when not ordered, expect first and last delete to be executed
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":1},"limit":0},{"q":{"$a":2},"limit":0},{"q":{"a":3},"limit":0}],"ordered":false}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 without filters is supported for unsharded collections
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 is retryable on unsharded collection (second call is a noop)
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":1}]}', NULL, 'xact-1');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":1}]}', NULL, 'xact-1');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 is supported in the _id case
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":6},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":6}';
rollback;

-- delete 1 is supported in the multiple identical _id case
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"_id":6},{"_id":6}]},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":6}';
rollback;

-- delete 1 is supported in the multiple distinct _id case (but a noop)
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"_id":6},{"_id":5}]},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":6}';
rollback;

-- validate _id extraction
begin;
set local citus.log_remote_commands to on;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":6},"limit":1}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"_id":6},{"_id":5}]},"limit":1}]}');
reset citus.log_remote_commands;
rollback;

-- shard the collection
select helio_api.shard_collection('db', 'removeme', '{"a":"hashed"}', false);

-- make sure we get the expected results after sharding a collection
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$lte":5}},"limit":0}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":1}';
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":10}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- test pruning logic in delete
begin;
select count(*) from helio_api.collection('db', 'removeme');
set local citus.log_remote_commands to on;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":5}},"limit":0}]}');
reset citus.log_remote_commands;
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":5}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

begin;
select count(*) from helio_api.collection('db', 'removeme');
set local citus.log_remote_commands to on;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"a":5},{"a":{"$gt":0}}]},"limit":0}]}');
reset citus.log_remote_commands;
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":5}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 without filters is unsupported for sharded collections
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":1}]}');

-- delete 1 with shard key filters is supported for sharded collections
begin;
select count(*) from helio_api.collection('db', 'removeme');
set local citus.log_remote_commands to on;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":5}},"limit":1}]}');
reset citus.log_remote_commands;
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":5}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 with shard key filters is retryable
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":5}},"limit":1}]}', NULL, 'xact-2');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":5}},"limit":1}]}', NULL, 'xact-2');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 that does not match any rows is still retryable
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":15}},"limit":1}]}', NULL, 'xact-3');
select 1 from helio_api.insert_one('db', 'removeme', '{"a":15,"_id":15}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$eq":15}},"limit":1}]}', NULL, 'xact-3');
rollback;

-- delete 1 is supported in the _id case even on sharded collections
begin;
-- add an additional _id 10
select 1 from helio_api.insert_one('db', 'removeme', '{"a":11,"_id":10}');
-- delete first row where _id = 10
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":10},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":10}';
-- delete second row where _id = 10
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":10},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":10}';
-- no more row where _id = 10
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":10},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":10}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 with with _id filter on a sharded collection is retryable
begin;
-- add an additional _id 10 (total to 11 rows)
select 1 from helio_api.insert_one('db', 'removeme', '{"a":11,"_id":10}');
-- delete first row where _id = 10
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":10},"limit":1}]}', NULL, 'xact-4');
-- second time is a noop
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":10},"limit":1}]}', NULL, 'xact-4');
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- delete 1 is supported in the multiple identical _id case
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"_id":6},{"_id":6}]},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":6}';
rollback;

-- delete 1 is unsupported in the multiple distinct _id case
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"_id":6},{"_id":5}]},"limit":1}]}');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"_id":6}';
rollback;

-- validate _id extraction
begin;
set local citus.log_remote_commands to on;
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a": 11, "_id":6},"limit":0}]}');
select helio_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"$and":[{"a": 11},{"_id":6},{"_id":5}]},"limit":0}]}');
reset citus.log_remote_commands;
rollback;

-- delete with spec in special section
begin;
select count(*) from helio_api.collection('db', 'removeme');
select helio_api.delete('db', '{"delete":"removeme"}', '{ "":[{"q":{"a":{"$eq":5}},"limit":1}] }');
select count(*) from helio_api.collection('db', 'removeme') where document @@ '{"a":5}';
select count(*) from helio_api.collection('db', 'removeme');
rollback;

-- deletes with both specs specified 
begin;
select helio_api.delete('db', '{"delete":"removeme", "deletes": [{"q":{"a":{"$eq":5}},"limit":1}] }', '{ "":[{"q":{"a":{"$eq":5}},"limit":1}] }');
rollback;

select helio_api.drop_collection('db','removeme');

SELECT 1 FROM helio_api.insert_one('delete', 'test_sort_returning', '{"_id": 1,"a":3,"b":7}');
SELECT 1 FROM helio_api.insert_one('delete', 'test_sort_returning', '{"_id": 2,"a":2,"b":5}');
SELECT 1 FROM helio_api.insert_one('delete', 'test_sort_returning', '{"_id": 3,"a":1,"b":6}');

-- sort in ascending order and project & return deleted document
WITH test_sort_returning AS (
    SELECT * FROM helio_api_catalog.collections WHERE database_name = 'delete' AND collection_name = 'test_sort_returning'
)
SELECT result.*
FROM test_sort_returning,
     helio_api_internal.delete_one(
        p_collection_id=>test_sort_returning.collection_id,
        p_shard_key_value=>test_sort_returning.collection_id,
        p_query=>'{"a": {"$gte": 1}}'::bson,
        p_sort=>'{"b": 1}'::bson,
        p_return_document=>true,
        p_return_fields=>'{"a": 0}'::bson,
        p_transaction_id=>null::text
     ) AS result;

-- sort by multiple fields (i) and return deleted document
BEGIN;
    WITH test_sort_returning AS (
        SELECT * FROM helio_api_catalog.collections WHERE database_name = 'delete' AND collection_name = 'test_sort_returning'
    )
    SELECT result.*
    FROM test_sort_returning,
         helio_api_internal.delete_one(
            p_collection_id=>test_sort_returning.collection_id,
            p_shard_key_value=>test_sort_returning.collection_id,
            p_query=>'{"a": {"$gte": 1}}'::bson,
            p_sort=>'{"b": -1, "a": 1}'::bson,
            p_return_document=>true,
            p_return_fields=>null::bson,
            p_transaction_id=>null::text
         ) AS result;
ROLLBACK;

-- sort by multiple fields (ii) and return deleted document
WITH test_sort_returning AS (
    SELECT * FROM helio_api_catalog.collections WHERE database_name = 'delete' AND collection_name = 'test_sort_returning'
)
SELECT result.*
FROM test_sort_returning,
     helio_api_internal.delete_one(
        p_collection_id=>test_sort_returning.collection_id,
        p_shard_key_value=>test_sort_returning.collection_id,
        p_query=>'{"a": {"$gte": 1}}'::bson,
        p_sort=>'{"a": 1, "b": -1}'::bson,
        p_return_document=>true,
        p_return_fields=>null::bson,
        p_transaction_id=>null::text
     ) AS result;

SELECT document FROM helio_api.collection('delete', 'test_sort_returning') ORDER BY 1;

-- show that we validate "query" document even if collection doesn't exist
-- i) ordered=true
SELECT helio_api.delete(
    'delete',
    '{
        "delete": "dne",
        "deletes": [
            {"q": {"a": 1}, "limit": 0 },
            {"q": {"$b": 1}, "limit": 0 },
            {"q": {"c": 1}, "limit": 0 },
            {"q": {"$d": 1}, "limit": 0 },
            {"q": {"e": 1}, "limit": 0 }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT helio_api.delete(
    'delete',
    '{
        "delete": "dne",
        "deletes": [
            {"q": {"a": 1}, "limit": 0 },
            {"q": {"$b": 1}, "limit": 0 },
            {"q": {"c": 1}, "limit": 0 },
            {"q": {"$d": 1}, "limit": 0 },
            {"q": {"e": 1}, "limit": 0 }
        ],
        "ordered": false
     }'
);

SELECT helio_api.create_collection('delete', 'no_match');

-- show that we validate "query" document even if we can't match any documents
-- i) ordered=true
SELECT helio_api.delete(
    'delete',
    '{
        "delete": "no_match",
        "deletes": [
            {"q": {"a": 1}, "limit": 0 },
            {"q": {"$b": 1}, "limit": 0 },
            {"q": {"c": 1}, "limit": 0 },
            {"q": {"$d": 1}, "limit": 0 },
            {"q": {"e": 1}, "limit": 0 }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT helio_api.delete(
    'delete',
    '{
        "delete": "no_match",
        "deletes": [
            {"q": {"a": 1}, "limit": 0 },
            {"q": {"$b": 1}, "limit": 0 },
            {"q": {"c": 1}, "limit": 0 },
            {"q": {"$d": 1}, "limit": 0 },
            {"q": {"e": 1}, "limit": 0 }
        ],
        "ordered": false
     }'
);
