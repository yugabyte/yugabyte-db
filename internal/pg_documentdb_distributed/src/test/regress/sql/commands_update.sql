SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 649000;
SET documentdb.next_collection_id TO 6490;
SET documentdb.next_collection_index_id TO 6490;

select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":1,"_id":1,"b":1}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":2,"_id":2,"b":2}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":3,"_id":3,"b":3}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":4,"_id":4,"b":4}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":5,"_id":5,"b":5}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":6,"_id":6,"b":6}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":7,"_id":7,"b":7}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":8,"_id":8,"b":8}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":9,"_id":9,"b":9}');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":10,"_id":10,"b":10}');

-- exercise invalid update syntax errors
select documentdb_api.update('db', NULL);
select documentdb_api.update(NULL, '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
select documentdb_api.update('db', '{"updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme"}');
select documentdb_api.update('db', '{"update":["updateme"], "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":{"q":{},"u":{"$set":{"b":0}},"multi":true}}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}], "extra":1}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{}}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"u":{"$set":{"b":0}}}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":"text"}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":[],"u":{"$set":{"b":0}},"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":1}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true,"extra":1}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}],"ordered":1}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$bork":{"b":0}},"multi":true}]}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"upsert":[]}]}');

-- Disallow writes to system.views
select documentdb_api.update('db', '{"update":"system.views", "updates":[{"q":{},"u":{"$set":{"b":0}}}]}');

-- update all
begin;
SET LOCAL search_path TO '';
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document OPERATOR(documentdb_api_catalog.@@) '{"b":0}';
rollback;

-- update some
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":{"$set":{"b":0}},"multi":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- update multi with a replace is not supported
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":{"b":0},"multi":true}]}');
rollback;

-- update multi with an aggregation pipeline is supported
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":[{"$unset":["b"]}],"multi":true}]}');
rollback;

-- update all from non-existent collection
select documentdb_api.update('db', '{"update":"notexists", "updates":[{"q":{},"u":{"$set":{"b":0}}}]}');

-- query syntax errors are added the response
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$ltr":5}},"u":{"$set":{"b":0}},"multi":true}]}');

-- when ordered, expect only first update to be executed
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":0}}},{"q":{"$a":2},"u":{"$set":{"b":0}}},{"q":{"a":3},"u":{"$set":{"b":0}}}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":0}}},{"q":{"$a":2},"u":{"$set":{"b":0}}},{"q":{"a":3},"u":{"$set":{"b":0}}}],"ordered":true}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- when not ordered, expect first and last update to be executed
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":0}}},{"q":{"$a":2},"u":{"$set":{"b":0}}},{"q":{"a":3},"u":{"$set":{"b":0}}}],"ordered":false}');
select count(*) from documentdb_api.collection('db', 'updateme');
rollback;

-- update 1 without filters is supported for unsharded collections
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":false}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- update 1 with a replace that preserves the _id
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":3},"u":{"b":0},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- update 1 with a replace that tries to change the _id
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":3},"u":{"_id":0,"b":0},"multi":false}]}');
rollback;

-- update 1 is retryable on unsharded collection (second call is a noop)
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-1');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-1');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":1}';
-- third call is considered a new try
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-1');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":1}';
rollback;

-- update 1 is supported in the _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":6},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":6}';
rollback;

-- update 1 is supported in the multiple identical _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":6}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":6}';
rollback;

-- update 1 is supported in the multiple distinct _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":5}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":6}';
rollback;

-- update some with range filter that excludes all rows
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$lt":0}},"u":{"$set":{"b":0,"_id":11}},"multi":true,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- upsert 1 with range filter is supported for unsharded collections
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"b":{"$lt":0}},"u":{"$set":{"b":0,"_id":11}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- upsert 1 with a replace that preserves the _id
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":11},"u":{"b":0},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- upsert 1 with a replace that set the _id
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33},"u":{"_id":0,"b":0},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":0}';
rollback;

-- upsert 1 is retryable on unsharded collection (second call is a noop)
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":66}';
-- third call is considered a new try
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":66}';
rollback;

-- upsert 1 is supported in the _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":0}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33}';
rollback;


-- test _id extraction from update
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":3}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":3}';
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":4}},"multi":false,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":4}';
rollback;

begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":0}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":0}';
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":1}},"multi":true,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":1}';
rollback;

-- shard the collection
select documentdb_api.shard_collection('db', 'updateme', '{"a":"hashed"}', false);

-- make sure we get the expected results after sharding a collection
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":5}},"u":{"$set":{"b":0}},"multi":true}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":1}';
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":10}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- test pruning logic in update
begin;
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":5}},"u":{"$set":{"b":0}},"multi":true}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

begin;
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"$and":[{"a":5},{"a":{"$gt":0}}]},"u":{"$set":{"b":0}},"multi":true}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- update 1 without filters is unsupported for sharded collections
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":false}]}');

-- update 1 with shard key filters is supported for sharded collections
begin;
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":5}},"u":{"$set":{"b":0}},"multi":false}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
rollback;

-- update 1 with shard key filters is retryable
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":10}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-2');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":10}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-2');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"a":10}';
rollback;

-- upsert 1 with shard key filters is retryable
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":66}';
-- third call is considered a new try
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"c":66}';
rollback;

-- update 1 that does not match any rows is still retryable
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":15}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-3');
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":15,"_id":15}');
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":15}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-3');
rollback;

-- update 1 is supported in the _id case even on sharded collections
begin;
-- add an additional _id 10
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":11,"_id":10}');
-- update first row where _id = 10
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":10}';
-- update second row where _id = 10
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":0}';
-- no more row where _id = 10 and b != 0
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');
rollback;

-- update 1 with with _id filter on a sharded collection is retryable
begin;
-- add an additional _id 10
select 1 from documentdb_api.insert_one('db', 'updateme', '{"a":11,"_id":10}');
-- update first row where _id = 10
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":10},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-4');
-- second time is a noop
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":10},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-4');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":10}' ORDER BY object_id;
rollback;

-- upsert on sharded collection with multi:true
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"b":33},"u":{"$set":{"b":33,"_id":11}},"multi":true,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"b":33}';
rollback;

-- updating shard key is disallowed when using multi:true
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"b":1},"u":{"$inc":{"a":1}},"multi":true}]}');
rollback;

-- updating shard key is disallowed when using multi:true, even with a shard key filter
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"a":1}},"multi":true}]}');
rollback;

-- updating shard key is disallowed without a shard key filter
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"b":1},"u":{"$inc":{"a":1}},"multi":false}]}');
rollback;

begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":1},"u":{"$inc":{"a":1}},"multi":false}]}');
rollback;

-- updating shard key is allowed when multi:false and shard key filter is specified
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"a":1}},"multi":false}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"a":2}';
rollback;

begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":10},"u":{"$set":{"a":20}},"multi":false}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"a":20}';
rollback;

-- empty shard key value is allowed (hash becomes 0)
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$unset":{"a":1}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"b":1}';
rollback;

-- update 1 is supported in the multiple identical _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":6}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":6}';
rollback;

-- update 1 is unsupported in the multiple distinct _id case
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":5}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":6}';
rollback;

-- test _id extraction from update
begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":3}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":3 }';
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":4}},"multi":false,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":4 }';
rollback;

begin;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":1}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":1 }';
set local citus.log_remote_commands to on;
select documentdb_api.update('db', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":2}},"multi":true,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{"_id":33, "b":2 }';
rollback;

-- update with docs specified on special section
begin;
select documentdb_api.update('db', '{"update":"updateme"}', '{ "":[{"q":{"_id":36, "a": 10 },"u":{"$set":{"bz":1}},"multi":false,"upsert":true}] }');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{ "bz":1 }';
select documentdb_api.update('db', '{"update":"updateme"}', '{ "":[{"q":{"_id":37, "a": 10 },"u":{"$set":{"bz":1}},"multi":false,"upsert":true}] }');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{ "bz":1 }';
select documentdb_api.update('db', '{"update":"updateme"}', '{ "":[{"q":{"_id":36, "a": 10 },"u":{"$set":{"bz":2}},"multi":false }, {"q":{"_id":37, "a": 10 },"u":{"$set":{"bz":2}},"multi":false}] }');
select count(*) from documentdb_api.collection('db', 'updateme') where document @@ '{ "bz":2 }';
rollback;

-- update with docs specified on both
begin;
select documentdb_api.update('db', '{"update":"updateme",  "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":2}},"multi":true,"upsert":true}]}', '{ "":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":1}},"multi":false,"upsert":true}] }');
rollback;

select documentdb_api.drop_collection('db','updateme');

SELECT 1 FROM documentdb_api.insert_one('update', 'test_sort_returning', '{"_id":1,"a":3,"b":7}');
SELECT 1 FROM documentdb_api.insert_one('update', 'test_sort_returning', '{"_id":2,"a":2,"b":5}');
SELECT 1 FROM documentdb_api.insert_one('update', 'test_sort_returning', '{"_id":3,"a":1,"b":6}');

-- sort in ascending order and project & return old document
SELECT collection_id AS test_sort_returning FROM documentdb_api_catalog.collections WHERE database_name = 'update' AND collection_name = 'test_sort_returning' \gset
SELECT documentdb_api_internal.update_worker(
    p_collection_id=>:test_sort_returning,
    p_shard_key_value=>:test_sort_returning,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "updateOne": { "query": { "a": {"$gte": 1} }, "update": { "": { "c": 3 } }, "isUpsert": false, "sort": { "b": 1 }, "returnDocument": 1, "returnFields": { "_id": 0, "b": 1 } } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
) FROM documentdb_api.collection('update', 'test_sort_returning');

-- sort by multiple fields (i) and project & return new document
BEGIN;
    SELECT documentdb_api_internal.update_worker(
        p_collection_id=>:test_sort_returning,
        p_shard_key_value=>:test_sort_returning,
        p_shard_oid => 0,
        p_update_internal_spec => '{ "updateOne": { "query": { "a": {"$gte": 1} }, "update": { "": { "c": 4 } }, "isUpsert": false, "sort": { "b": -1, "a": 1 }, "returnDocument": 2 } }'::bson,
        p_update_internal_docs=>null::bsonsequence,
        p_transaction_id=>null::text
    ) FROM documentdb_api.collection('update', 'test_sort_returning');

    SELECT document FROM documentdb_api.collection('update', 'test_sort_returning') ORDER BY 1;
ROLLBACK;

SELECT documentdb_api_internal.update_worker(
    p_collection_id=>:test_sort_returning,
    p_shard_key_value=>:test_sort_returning,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "updateOne": { "query": { "a": {"$gte": 1} }, "update": { "": { "c": 4 } }, "isUpsert": false, "sort": { "a": 1, "b": -1 }, "returnDocument": 2 } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
    ) FROM documentdb_api.collection('update', 'test_sort_returning');

SELECT document FROM documentdb_api.collection('update', 'test_sort_returning') ORDER BY 1;

-- show that we validate "update" document even if collection doesn't exist
-- i) ordered=true
SELECT documentdb_api.update(
    'update',
    '{
        "update": "dne",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"b": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"d": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT documentdb_api.update(
    'update',
    '{
        "update": "dne",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"b": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"d": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": false
     }'
);

-- show that we validate "query" document even if collection doesn't exist
-- i) ordered=true
SELECT documentdb_api.update(
    'update',
    '{
        "update": "dne",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$b": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$d": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT documentdb_api.update(
    'update',
    '{
        "update": "dne",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$b": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$d": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": false
     }'
);

SELECT documentdb_api.create_collection('update', 'no_match');

-- show that we validate "update" document even if we can't match any documents
-- i) ordered=true
SELECT documentdb_api.update(
    'update',
    '{
        "update": "no_match",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"b": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"d": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT documentdb_api.update(
    'update',
    '{
        "update": "no_match",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"b": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"d": 1}, "u": { "$set": { "p": 1 }, "$unset": {"p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": false
     }'
);

-- show that we validate "query" document even if we can't match any documents
-- i) ordered=true
SELECT documentdb_api.update(
    'update',
    '{
        "update": "no_match",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$b": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$d": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": true
     }'
);
-- ii) ordered=false
SELECT documentdb_api.update(
    'update',
    '{
        "update": "no_match",
        "updates": [
            {"q": {"a": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$b": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"c": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"$d": 1}, "u": { "$set": { "p": 1 } } },
            {"q": {"e": 1}, "u": { "$set": { "p": 1 } } }
        ],
        "ordered": false
     }'
);


-- Validate multi and single updates with no-op return matched and updated as expected
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":1,"a":1,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":2,"a":2,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":3,"a":3,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":4,"a":100,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":5,"a":200,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update', 'multi', '{"_id":6,"a":6,"b":1}');

select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":3}},"multi":true,"upsert":false}]}');
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":7}},"multi":true,"upsert":false}]}');
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":150}},"multi":true,"upsert":false}]}');
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":99}},"multi":true,"upsert":false}]}');

-- no match
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 50 },"u":{"$min":{"a":99}},"multi":true,"upsert":false}]}');

-- all updated
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":500}},"multi":true,"upsert":false}]}');

-- all match, no-op update
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":100}},"multi":true,"upsert":false}]}');

-- single update
SELECT 1 FROM documentdb_api.insert_one('update', 'single', '{"_id":1,"a":1,"b":1}');

select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":100}},"multi":false,"upsert":false}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":50}},"multi":false,"upsert":false}]}');

-- no-op update single
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":50}},"multi":false,"upsert":false}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":50}},"multi":false,"upsert":false}]}');

-- no match
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"b": 50 },"u":{"$max":{"a":50}},"multi":false,"upsert":false}]}');

--upsert with same field in querySpec 
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":1234, "$and" : [{"x" : {"$eq": 1}}, {"x": 2}]},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":2345, "x": 1, "x.x": 1},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":3456, "x": {}, "x.x": 1},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":4567, "x": {"x": 1}, "x.x": 1},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":5678, "x": {"x": 1}, "x.y": 1},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id":6789, "x": [1, {"x": 1}], "x.x": 1},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"x": 1, "x": 5},"u":{"$set" : {"x":10}},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id": 1, "_id.b": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_idab.b": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id.c": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{"_id":3},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{"$set" : {"_id":3} },"upsert":true}]}');

-- array filters sent to final update command.
BEGIN;
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"_id": 134111, "b": [ 5, 2, 4 ] },"u":{"$set" : {"b.$[a]":3} },"upsert":true, "arrayFilters": [ { "a": 2 } ]}]}');

SELECT documentdb_api.insert_one('update', 'multi', '{ "_id": 134112, "blah": [ 5, 1, 4 ] }');
SELECT documentdb_api.insert_one('update', 'multi', '{ "_id": 134113, "blah": [ 6, 3, 1 ] }');
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"blah": 1 },"u":{"$max":{"blah.$[a]":99}},"multi":true,"upsert":false, "arrayFilters": [ { "a": 1 } ]}]}');

-- no match here.
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"blah": 1 },"u":{"$max":{"blah.$[a]":99}},"multi":false,"upsert":false, "arrayFilters": [ { "a": 1 } ]}]}');

-- updates one of the two.
select documentdb_api.update('update', '{"update":"multi", "updates":[{"q":{"blah": 99 },"u":{"$max":{"blah.$[a]":109}},"multi":false,"upsert":false, "arrayFilters": [ { "a": 99 } ]}]}');
ROLLBACK;

-- test replace with upsert
select documentdb_api.update('test@', '{ "update" : "shell_wc_a", "ordered" : true, "updates" : [ { "q" : { "_id" : 1.0 }, "u" : { "_id" : 1.0 }, "multi" : false, "upsert" : true } ] }');

--upsert with $expr
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$and" : [{"$expr" : {"$eq": ["$a",1]}}]},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$or" : [{"$expr" : {"$gt": ["$b",1]}}, {"x": 2}]},"u":{"$set" : {"a": 10}},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$or" : [{"$expr" : {"$gte": ["$c", 100]}}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$or" : [{"$or" : [{"$expr" : {"$gte": ["$c", 100]}}]}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$and" : [{"$or" : [{"x": 10},{"$expr" : {"$gte": ["$c", 100]}}, {"y": 10}]}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$expr": {"$gt" : ["$x",10]}},"u":{},"upsert":true}]}');
select documentdb_api.update('update', '{"update":"single", "updates":[{"q":{"$and" : [{"a": 10}, {"$or" : [{"x": 10},{"$expr" : {"$gte": ["$c", 100]}}, {"y": 10}]}, {"b":11} ]},"u":{"$set": {"a" :10}},"upsert":true}]}');

-- below query will be updating document although we don't have @@ in query match but for below query only object id filter is sufficient.

BEGIN;

SELECT documentdb_api.insert_one('update','NonID',' { "_id" :  1, "b" : 1 }', NULL);

set local citus.log_remote_commands to on;
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"a":{"$eq":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"a":{"$eq":1}, "_id" : 1},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$eq":1}, "a" : 1},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$gt":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$lt":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": []}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": [2]}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": [2,3,4]}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1,2]}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$expr":{"$gt": ["$_id",1]}},"u":{"$set":{"b":0 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$and":[{"_id":1},{"_id":2}]},"u":{"$set":{"b":0}},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":2}, {"a" : 1}]},"u":{"$set":{"b":0}},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$or":[{"b":2}]},"u":{"$set":{"b":0}},"multi":false}]}');

SELECT document from documentdb_api.collection('update', 'NonID');

-- below query will be updating document although we don't have @@ in query match but for below query only object id filter is sufficient.
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$eq":1}},"u":{"$inc":{"b": 1 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$and":[{"_id":1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":1}, {"b" : 1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1]}},"u":{"$inc":{"b":1 }},"multi":false}]}');
SELECT documentdb_api.update('update', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1,1]}},"u":{"$inc":{"b":1 }},"multi":false}]}');
SELECT document from documentdb_api.collection('update', 'NonID');

ROLLBACK;

-- regex operator and update
SELECT documentdb_api.insert_one('db', 'regexColl', '{ "_id" : 1, "Login": "robert.bean@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('db', 'regexColl', '{ "_id" : 2, "Login": "peter.ramesh@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('db', 'regexColl', '{ "_id" : 3, "Login": "picop1@test.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('db', 'regexColl', '{ "_id" : 4, "Login": "peter.claxton@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.update('db', '{"update":"regexColl", "updates":[{ "q" : { "Login" : { "$regularExpression" : { "pattern" : "^picop1@test\\.co\\.uk$", "options" : "i" } } },"u":{"$addToSet": { "RefreshTokens": { "RefreshToken": "eyJhbGciOiJSUzI1NiIsImtpZCI6ImYxNDNhY2ZmMzNjODQ" } }}}]}');
SELECT document from documentdb_api.collection('db', 'regexColl');

-- test update with upsert
BEGIN;
set documentdb.useLocalExecutionShardQueries to off;
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

SELECT documentdb_api.shard_collection('update', 'single', '{ "_id": "hashed" }', false);

SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id":8011 }, "u": { "value": "1" }, "upsert": true }] }');
ROLLBACK;

-- test update for upsert error cases
BEGIN;

-- this inserts the document
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "value": "1123" }, "u": { "_id": 8010 }, "upsert": true }] }');
ROLLBACK;


-- test update for upsert error cases
BEGIN;

-- this inserts the document
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "value": "21020" }, "u": { "$set": { "_id": 8010 } }, "upsert": true, "multi": true }] }');
ROLLBACK;


-- test update for upsert error cases
BEGIN;

-- this inserts the document
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
SELECT documentdb_api.update('update', '{ "update": "single", "updates": [ { "q": { "_id": 8010, "a": 5 }, "u": { "$set": { "c": 8010 } }, "upsert": true, "multi": true }] }');
ROLLBACK;
