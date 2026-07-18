SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 849000;
SET documentdb.next_collection_id TO 8490;
SET documentdb.next_collection_index_id TO 8490;

SET documentdb.EnableVariablesSupportForWriteCommands TO on;

select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":1,"_id":1,"b":1}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":2,"_id":2,"b":2}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":3,"_id":3,"b":3}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":4,"_id":4,"b":4}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":5,"_id":5,"b":5}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":6,"_id":6,"b":6}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":7,"_id":7,"b":7}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":8,"_id":8,"b":8}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":9,"_id":9,"b":9}');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":10,"_id":10,"b":10}');

-- exercise invalid update syntax errors
CALL documentdb_api.update_bulk('bulkdb', NULL);
CALL documentdb_api.update_bulk(NULL, '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme"}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":["updateme"], "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":{"q":{},"u":{"$set":{"b":0}},"multi":true}}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}], "extra":1}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{}}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"u":{"$set":{"b":0}}}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":"text"}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":[],"u":{"$set":{"b":0}},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":1}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true,"extra":1}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}],"ordered":1}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$bork":{"b":0}},"multi":true}]}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"upsert":[]}]}');

-- Disallow writes to system.views
CALL documentdb_api.update_bulk('bulkdb', '{"update":"system.views", "updates":[{"q":{},"u":{"$set":{"b":0}}}]}');

-- update all

set search_path TO '';
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":true}]}');
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document OPERATOR(documentdb_api_catalog.@@) '{"b":0}';


-- update some

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":{"$set":{"b":1}},"multi":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":1}';


-- update multi with a replace is not supported

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":{"b":0},"multi":true}]}');


-- update multi with an aggregation pipeline is supported

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":3}},"u":[{"$unset":["b"]}],"multi":true}]}');


-- update all from non-existent collection
CALL documentdb_api.update_bulk('bulkdb', '{"update":"notexists", "updates":[{"q":{},"u":{"$set":{"b":0}}}]}');

-- query syntax errors are added the response
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$ltr":5}},"u":{"$set":{"b":0}},"multi":true}]}');

-- when ordered, expect only first update to be executed
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":2}}},{"q":{"$a":2},"u":{"$set":{"b":2}}},{"q":{"a":3},"u":{"$set":{"b":2}}}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":2}';


CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":3}}},{"q":{"$a":2},"u":{"$set":{"b":3}}},{"q":{"a":3},"u":{"$set":{"b":3}}}],"ordered":true}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":3}';


-- when not ordered, expect first and last update to be executed

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$set":{"b":0}}},{"q":{"$a":2},"u":{"$set":{"b":0}}},{"q":{"a":3},"u":{"$set":{"b":0}}}],"ordered":false}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme');


-- update 1 without filters is supported for unsharded collections

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":5}},"multi":false}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":5}';


-- update 1 with a replace that preserves the _id

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":3},"u":{"b":6},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":6}';


-- update 1 with a replace that tries to change the _id

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":3},"u":{"_id":0,"b":7},"multi":false}]}');


-- update 1 is retryable on unsharded collection (second call is a noop)

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":8}},"multi":false}]}', NULL, 'xact-1');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":8}},"multi":false}]}', NULL, 'xact-1');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":1}';
-- third call is considered a new try
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"b":8}},"multi":false}]}', NULL, 'xact-1');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":1}';


-- update 1 is supported in the _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":6},"u":{"$set":{"b":9}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":6}';


-- update 1 is supported in the multiple identical _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":6}]},"u":{"$set":{"b":10}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":6}';


-- update 1 is supported in the multiple distinct _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":5}]},"u":{"$set":{"b":11}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":6}';


-- update some with range filter that excludes all rows

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$lt":0}},"u":{"$set":{"b":12,"_id":11}},"multi":true,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":12}';


-- upsert 1 with range filter is supported for unsharded collections

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"b":{"$lt":0}},"u":{"$set":{"b":0,"_id":11}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';


-- upsert 1 with a replace that preserves the _id

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":11},"u":{"b":13},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":13}';


-- upsert 1 with a replace that set the _id

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33},"u":{"_id":0,"b":14},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":0}';


-- upsert 1 is retryable on unsharded collection (second call is a noop)

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":66}';
-- third call is considered a new try
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":66}';


-- upsert 1 is supported in the _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":0}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33}';



-- test _id extraction from update

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":3}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":3}';
set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":4}},"multi":false,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":4}';


CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":0}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":0}';
set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33},"u":{"$set":{"b":1}},"multi":true,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":1}';


-- shard the collection
select documentdb_api.shard_collection('bulkdb', 'updateme', '{"a":"hashed"}', false);

-- make sure we get the expected results after sharding a collection

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$lte":5}},"u":{"$set":{"b":0}},"multi":true}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":1}';
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":10}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';


-- test pruning logic in update

set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":5}},"u":{"$set":{"b":0}},"multi":true}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';



set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"$and":[{"a":5},{"a":{"$gt":0}}]},"u":{"$set":{"b":0}},"multi":true}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';


-- update 1 without filters is unsupported for sharded collections
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{},"u":{"$set":{"b":0}},"multi":false}]}');

-- update 1 with shard key filters is supported for sharded collections

set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":5}},"u":{"$set":{"b":0}},"multi":false}]}');
reset citus.log_remote_commands;
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":5}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';


-- update 1 with shard key filters is retryable

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":10}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-2');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":10}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-2');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":10}';


-- upsert 1 with shard key filters is retryable

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":66}';
-- third call is considered a new try
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":33,"_id":11},"u":{"$inc":{"c":33}},"multi":false,"upsert":true}]}', NULL, 'xact-ups');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":33}';
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"c":66}';


-- update 1 that does not match any rows is still retryable

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":15}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-3');
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":15,"_id":15}');
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":{"$eq":15}},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-3');


-- update 1 is supported in the _id case even on sharded collections

-- add an additional _id 10
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":11,"_id":10}');
-- update first row where _id = 10
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":10}';
-- update second row where _id = 10
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":0}';
-- no more row where _id = 10 and b != 0
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}]}');


-- update 1 with with _id filter on a sharded collection is retryable

-- add an additional _id 10
select 1 from documentdb_api.insert_one('bulkdb', 'updateme', '{"a":11,"_id":10}');
-- update first row where _id = 10
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":10},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-4');
-- second time is a noop
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":10},"u":{"$inc":{"b":1}},"multi":false}]}', NULL, 'xact-4');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":10}' ORDER BY object_id;


-- upsert on sharded collection with multi:true

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"b":33},"u":{"$set":{"b":33,"_id":11}},"multi":true,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":33}';


-- updating shard key is disallowed when using multi:true

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"b":1},"u":{"$inc":{"a":1}},"multi":true}]}');


-- updating shard key is disallowed when using multi:true, even with a shard key filter

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"a":1}},"multi":true}]}');


-- updating shard key is disallowed without a shard key filter

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"b":1},"u":{"$inc":{"a":1}},"multi":false}]}');



CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":1},"u":{"$inc":{"a":1}},"multi":false}]}');


-- updating shard key is allowed when multi:false and shard key filter is specified

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$inc":{"a":1}},"multi":false}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":2}';



CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":10},"u":{"$set":{"a":20}},"multi":false}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"a":20}';


-- empty shard key value is allowed (hash becomes 0)

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"a":1},"u":{"$unset":{"a":1}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"b":1}';


-- update 1 is supported in the multiple identical _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":6}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":6}';


-- update 1 is unsupported in the multiple distinct _id case

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"$and":[{"_id":6},{"_id":5}]},"u":{"$set":{"b":0}},"multi":false}]}');
select document from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":6}';


-- test _id extraction from update

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":3}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":3 }';
set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":4}},"multi":false,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":4 }';



CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":1}},"multi":false,"upsert":true}]}');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":1 }';
set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme", "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":2}},"multi":true,"upsert":true}]}');
reset citus.log_remote_commands;
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{"_id":33, "b":2 }';


-- update with docs specified on special section

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme"}', '{ "":[{"q":{"_id":36, "a": 10 },"u":{"$set":{"bz":1}},"multi":false,"upsert":true}] }');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{ "bz":1 }';
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme"}', '{ "":[{"q":{"_id":37, "a": 10 },"u":{"$set":{"bz":1}},"multi":false,"upsert":true}] }');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{ "bz":1 }';
CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme"}', '{ "":[{"q":{"_id":36, "a": 10 },"u":{"$set":{"bz":2}},"multi":false }, {"q":{"_id":37, "a": 10 },"u":{"$set":{"bz":2}},"multi":false}] }');
select count(*) from documentdb_api.collection('bulkdb', 'updateme') where document @@ '{ "bz":2 }';


-- update with docs specified on both

CALL documentdb_api.update_bulk('bulkdb', '{"update":"updateme",  "updates":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":2}},"multi":true,"upsert":true}]}', '{ "":[{"q":{"_id":33, "a": 10 },"u":{"$set":{"b":1}},"multi":false,"upsert":true}] }');


select documentdb_api.drop_collection('bulkdb','updateme');

SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'test_sort_returning', '{"_id":1,"a":3,"b":7}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'test_sort_returning', '{"_id":2,"a":2,"b":5}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'test_sort_returning', '{"_id":3,"a":1,"b":6}');

-- show that we validate "update" document even if collection doesn't exist
-- i) ordered=true
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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

SELECT documentdb_api.create_collection('update_bulk', 'no_match');

-- show that we validate "update" document even if we can't match any documents
-- i) ordered=true
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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
CALL documentdb_api.update_bulk(
    'update_bulk',
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
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":1,"a":1,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":2,"a":2,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":3,"a":3,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":4,"a":100,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":5,"a":200,"b":1}');
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'multi', '{"_id":6,"a":6,"b":1}');

CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":3}},"multi":true,"upsert":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":7}},"multi":true,"upsert":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":150}},"multi":true,"upsert":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":99}},"multi":true,"upsert":false}]}');

-- no match
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 50 },"u":{"$min":{"a":99}},"multi":true,"upsert":false}]}');

-- all updated
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":500}},"multi":true,"upsert":false}]}');

-- all match, no-op update
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":100}},"multi":true,"upsert":false}]}');

-- single update
SELECT 1 FROM documentdb_api.insert_one('update_bulk', 'single', '{"_id":1,"a":1,"b":1}');

CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":100}},"multi":false,"upsert":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":50}},"multi":false,"upsert":false}]}');

-- no-op update single
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$min":{"a":50}},"multi":false,"upsert":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"b": 1 },"u":{"$max":{"a":50}},"multi":false,"upsert":false}]}');

-- no match
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"b": 50 },"u":{"$max":{"a":50}},"multi":false,"upsert":false}]}');

--upsert with same field in querySpec 
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":1234, "$and" : [{"x" : {"$eq": 1}}, {"x": 2}]},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":2345, "x": 1, "x.x": 1},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":3456, "x": {}, "x.x": 1},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":4567, "x": {"x": 1}, "x.x": 1},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":5678, "x": {"x": 1}, "x.y": 1},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id":6789, "x": [1, {"x": 1}], "x.x": 1},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"x": 1, "x": 5},"u":{"$set" : {"x":10}},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id": 1, "_id.b": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_idab.b": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id.c": 2},"u":{"_id":3, "a":4},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{"_id":3},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id": 1, "_id": 2},"u":{"$set" : {"_id":3} },"upsert":true}]}');

-- array filters sent to final update command.
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"_id": 134111, "b": [ 5, 2, 4 ] },"u":{"$set" : {"b.$[a]":3} },"upsert":true, "arrayFilters": [ { "a": 2 } ]}]}');

SELECT documentdb_api.insert_one('update_bulk', 'multi', '{ "_id": 134112, "blah": [ 5, 1, 4 ] }');
SELECT documentdb_api.insert_one('update_bulk', 'multi', '{ "_id": 134113, "blah": [ 6, 3, 1 ] }');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"blah": 1 },"u":{"$max":{"blah.$[a]":99}},"multi":true,"upsert":false, "arrayFilters": [ { "a": 1 } ]}]}');

-- no match here.
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"blah": 1 },"u":{"$max":{"blah.$[a]":99}},"multi":false,"upsert":false, "arrayFilters": [ { "a": 1 } ]}]}');

-- updates one of the two.
CALL documentdb_api.update_bulk('update_bulk', '{"update":"multi", "updates":[{"q":{"blah": 99 },"u":{"$max":{"blah.$[a]":109}},"multi":false,"upsert":false, "arrayFilters": [ { "a": 99 } ]}]}');

-- test replace with upsert
CALL documentdb_api.update_bulk('bulktest@', '{ "update" : "shell_wc_a", "ordered" : true, "updates" : [ { "q" : { "_id" : 1.0 }, "u" : { "_id" : 1.0 }, "multi" : false, "upsert" : true } ] }');

--upsert with $expr
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$and" : [{"$expr" : {"$eq": ["$a",1]}}]},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$or" : [{"$expr" : {"$gt": ["$b",1]}}, {"x": 2}]},"u":{"$set" : {"a": 10}},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$or" : [{"$expr" : {"$gte": ["$c", 100]}}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$or" : [{"$or" : [{"$expr" : {"$gte": ["$c", 100]}}]}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$and" : [{"$or" : [{"x": 10},{"$expr" : {"$gte": ["$c", 100]}}, {"y": 10}]}]},"u":{"$set": {"a" :10}},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$expr": {"$gt" : ["$x",10]}},"u":{},"upsert":true}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"single", "updates":[{"q":{"$and" : [{"a": 10}, {"$or" : [{"x": 10},{"$expr" : {"$gte": ["$c", 100]}}, {"y": 10}]}, {"b":11} ]},"u":{"$set": {"a" :10}},"upsert":true}]}');

-- below query will be updating document although we don't have @@ in query match but for below query only object id filter is sufficient.

SELECT documentdb_api.insert_one('update_bulk','NonID',' { "_id" :  1, "b" : 1 }', NULL);

set citus.log_remote_commands to on;
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"a":{"$eq":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"a":{"$eq":1}, "_id" : 1},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$eq":1}, "a" : 1},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$gt":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$lt":1}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": []}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": [2]}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$in": [2,3,4]}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1,2]}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$expr":{"$gt": ["$_id",1]}},"u":{"$set":{"b":0 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$and":[{"_id":1},{"_id":2}]},"u":{"$set":{"b":0}},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":2}, {"a" : 1}]},"u":{"$set":{"b":0}},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$or":[{"b":2}]},"u":{"$set":{"b":0}},"multi":false}]}');

SELECT document from documentdb_api.collection('update_bulk', 'NonID');

-- below query will be updating document although we don't have @@ in query match but for below query only object id filter is sufficient.
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$eq":1}},"u":{"$inc":{"b": 1 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$and":[{"_id":1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":1}, {"b" : 1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"$or":[{"_id":1}]},"u":{"$inc":{"b":1}},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1]}},"u":{"$inc":{"b":1 }},"multi":false}]}');
CALL documentdb_api.update_bulk('update_bulk', '{"update":"NonID", "updates":[{"q":{"_id":{"$all": [1,1]}},"u":{"$inc":{"b":1 }},"multi":false}]}');
SELECT document from documentdb_api.collection('update_bulk', 'NonID');

-- regex operator and update
RESET citus.log_remote_commands;
RESET citus.log_local_commands;

SELECT documentdb_api.insert_one('bulkdb', 'regexColl', '{ "_id" : 1, "Login": "robert.bean@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('bulkdb', 'regexColl', '{ "_id" : 2, "Login": "peter.ramesh@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('bulkdb', 'regexColl', '{ "_id" : 3, "Login": "picop1@test.co.uk", "RefreshTokens": [ ] }', NULL);
SELECT documentdb_api.insert_one('bulkdb', 'regexColl', '{ "_id" : 4, "Login": "peter.claxton@networkrail.co.uk", "RefreshTokens": [ ] }', NULL);
CALL documentdb_api.update_bulk('bulkdb', '{"update":"regexColl", "updates":[{ "q" : { "Login" : { "$regularExpression" : { "pattern" : "^picop1@test\\.co\\.uk$", "options" : "i" } } },"u":{"$addToSet": { "RefreshTokens": { "RefreshToken": "eyJhbGciOiJSUzI1NiIsImtpZCI6ImYxNDNhY2ZmMzNjODQ" } }}}]}');
SELECT document from documentdb_api.collection('bulkdb', 'regexColl');

-- test update with upsert
set documentdb.useLocalExecutionShardQueries to off;
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

SELECT documentdb_api.shard_collection('update_bulk', 'single', '{ "_id": "hashed" }', false);

CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id":8011 }, "u": { "value": "1" }, "upsert": true }] }');

-- test update for upsert error cases

-- this inserts the document
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "value": "1123" }, "u": { "_id": 8010 }, "upsert": true }] }');


-- test update for upsert error cases


-- this inserts the document
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "value": "21020" }, "u": { "$set": { "_id": 8010 } }, "upsert": true, "multi": true }] }');


-- test update for upsert error cases
-- this inserts the document
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id":8010 }, "u": { "value": "1" }, "upsert": true }] }');

-- this will collide and produce a unique conflict.
CALL documentdb_api.update_bulk('update_bulk', '{ "update": "single", "updates": [ { "q": { "_id": 8010, "a": 5 }, "u": { "$set": { "c": 8010 } }, "upsert": true, "multi": true }] }');

reset documentdb.useLocalExecutionShardQueries;

-- update bulk transaction testing
SELECT documentdb_api.insert_one('update_bulk', 'test_update_batch', '{ "_id": 1, "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('update_bulk', 'test_update_batch', '{ "_id": 2, "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('update_bulk', 'test_update_batch', '{ "_id": 3, "a": 1, "b": 1 }');

SELECT FORMAT('{ "update": "test_update_batch", "updates": [ %s ]}', string_agg('{ "q": { "_id": 1 }, "u": { "$inc": { "b": 1 } }}', ',' )) AS update_query FROM generate_series(1, 10) \gset

-- update query with an invalid query in the middle
SELECT FORMAT('{ "update": "test_update_batch", "updates": [ %s, { "q": { "$a": 1 }, "u": { "$inc": { "b": 1 } } }, %s ]}',
    string_agg('{ "q": { "_id": 2 }, "u": { "$inc": { "b": 1 } }}', ',' ),
    string_agg('{ "q": { "_id": 2 }, "u": { "$inc": { "b": 1 } }}', ',' )) AS update_query2 FROM generate_series(1, 5) \gset

SELECT FORMAT('{ "update": "test_update_batch", "updates": [ %s, { "q": { "$a": 1 }, "u": { "$inc": { "b": 1 } } }, %s ], "ordered": false }',
    string_agg('{ "q": { "_id": 3 }, "u": { "$inc": { "b": 1 } }}', ',' ),
    string_agg('{ "q": { "_id": 3 }, "u": { "$inc": { "b": 1 } }}', ',' )) AS update_query3 FROM generate_series(1, 5) \gset

SET client_min_messages TO DEBUG1;

-- this one should have no commits logged here
CALL documentdb_api.update_bulk('update_bulk', :'update_query');

-- this should have 5 messages (1 try for the batch + rollback, and then 5 individual updates and stops).
CALL documentdb_api.update_bulk('update_bulk', :'update_query2');

-- This should have 10 messages (1 try for the batch + rollback, and retry 9 more)
CALL documentdb_api.update_bulk('update_bulk', :'update_query3');

-- should have updated 10 times
SELECT document FROM documentdb_api.collection('update_bulk', 'test_update_batch') ORDER BY object_id;

-- reduce batch to 8
set documentdb.batchWriteSubTransactionCount to 8;

-- This should have 1 commit message (1 for batchsize of 8 )
CALL documentdb_api.update_bulk('update_bulk', :'update_query');

-- this should have 5 messages (1 try for the batch + rollback, and then 5 individual updates and stops).
CALL documentdb_api.update_bulk('update_bulk', :'update_query2');

-- This should have 9 messages (1 try for the batch + rollback, and retry 8 more)
CALL documentdb_api.update_bulk('update_bulk', :'update_query3');

-- still increments
SELECT document FROM documentdb_api.collection('update_bulk', 'test_update_batch') ORDER BY object_id;

-- now reduce to 3
set documentdb.batchWriteSubTransactionCount to 3;

-- This should have 3 commit message (1 for each batchsize of 3)
CALL documentdb_api.update_bulk('update_bulk', :'update_query');

-- this should have 4 messages (1 for success, 1 try for the batch + rollback, and then 2 individual updates and stops).
CALL documentdb_api.update_bulk('update_bulk', :'update_query2');

-- this should have 7 messages (1 for success, 1 try for batch and rollback, and 5 more tries).
CALL documentdb_api.update_bulk('update_bulk', :'update_query3');

-- still increments
SELECT document FROM documentdb_api.collection('update_bulk', 'test_update_batch') ORDER BY object_id;

-- disable local writes
set documentdb.useLocalExecutionShardQueries to off;

-- These should all have 4 commit message (1 for each batchsize of 3, and 1 for the last one)
CALL documentdb_api.update_bulk('update_bulk', :'update_query');
CALL documentdb_api.update_bulk('update_bulk', :'update_query2');
CALL documentdb_api.update_bulk('update_bulk', :'update_query3');

-- still increments
SELECT document FROM documentdb_api.collection('update_bulk', 'test_update_batch') ORDER BY object_id;
