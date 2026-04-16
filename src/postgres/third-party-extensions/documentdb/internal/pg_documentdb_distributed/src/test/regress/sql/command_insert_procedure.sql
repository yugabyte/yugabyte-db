SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 129000;
SET documentdb.next_collection_id TO 1290;
SET documentdb.next_collection_index_id TO 1290;

-- Run inside transaction
BEGIN;
CALL documentdb_api.insert_bulk('db', '{"insert":"salaries", "documents":[{ "_id" : 1 }]}');
END;

-- exercise invalid insert syntax errors
CALL documentdb_api.insert_bulk('db', NULL);
CALL documentdb_api.insert_bulk(NULL, '{"insert":"collection", "documents":[{"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"documents":[{"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection"}');
CALL documentdb_api.insert_bulk('db', '{"insert":["collection"], "documents":[{"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection", "documents":{"a":1}}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection", "documents":[{"a":1}], "extra":1}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection", "documents":[4]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection", "documents":[{"a":1}],"ordered":1}');

-- Disallow system.views, system.profile writes
CALL documentdb_api.insert_bulk('db', '{"insert":"system.views", "documents":[{"a":1}],"ordered":true}');
CALL documentdb_api.insert_bulk('db', '{"insert":"system.profile", "documents":[{"a":1}],"ordered":true}');

-- regular single-row insert
CALL documentdb_api.insert_bulk('db', '{"insert":"collection0", "documents":[{"_id":99,"a":99}]}');
select document from documentdb_api.collection('db','collection0') where document @@ '{}';

-- batch scenario
set documentdb.batchWriteSubTransactionCount TO 5;

-- when single batch has no issue
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl0", "documents":[{"_id":1}, {"_id":2}, {"_id":3},  {"_id":4},  {"_id":5}]}');
select document from documentdb_api.collection('batchDB','batchColl0') where document @@ '{}';

-- when multiple batches have no issue
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl1", "documents":[{"_id":1}, {"_id":2}, {"_id":3},  {"_id":4},  {"_id":5}, {"_id":6}, {"_id":7}]}');
select document from documentdb_api.collection('batchDB','batchColl1') where document @@ '{}';

-- when single batch has issue _id 2 is duplicated
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl2", "documents":[{"_id":1}, {"_id":2}, {"_id":2},  {"_id":4},  {"_id":5}]}');
select document from documentdb_api.collection('batchDB','batchColl2') where document @@ '{}';

-- when multiple batches have no _id 2 is duplicated and _id 6 is duplicated
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl3", "documents":[{"_id":1}, {"_id":2}, {"_id":2},  {"_id":4},  {"_id":5}, {"_id":6}, {"_id":6}]}');
select document from documentdb_api.collection('batchDB','batchColl3') where document @@ '{}';

-- when single batch has issue _id 2 is duplicated and ordered false
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl4", "documents":[{"_id":1}, {"_id":2}, {"_id":2},  {"_id":4},  {"_id":5}], "ordered":false}');
select document from documentdb_api.collection('batchDB','batchColl4') where document @@ '{}';

-- when multiple batches have no _id 2 is duplicated and _id 6 is duplicated and ordered false
CALL documentdb_api.insert_bulk('batchDB', '{"insert":"batchColl5", "documents":[{"_id":1}, {"_id":2}, {"_id":2},  {"_id":4},  {"_id":5}, {"_id":6}, {"_id":6}], "ordered":false}');
select document from documentdb_api.collection('batchDB','batchColl5') where document @@ '{}';

-- end batch test rest GUC
set documentdb.batchWriteSubTransactionCount TO 512;

-- Insert collection a db with same name and different case and collection being same. Expect to error
CALL documentdb_api.insert_bulk('dB', '{"insert":"collection0", "documents":[{"_id":99,"a":99}]}');

-- Insert collection a db with same name and different case and collection being different. Expect to error
CALL documentdb_api.insert_bulk('dB', '{"insert":"collection9", "documents":[{"_id":99,"a":99}]}');

-- Insert collection same db and new collection.
CALL documentdb_api.insert_bulk('db', '{"insert":"collection8", "documents":[{"_id":99,"a":99}]}');


CALL documentdb_api.insert_bulk('db', '{"insert":"collection0", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection0", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
select document from documentdb_api.collection('db','collection0') where document @@ '{}';


CALL documentdb_api.insert_bulk('db', '{"insert":"collection1", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','collection1') where document @@ '{}' order by document-> '_id';


CALL documentdb_api.insert_bulk('db', '{"insert":"collection2", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','collection2') where document @@ '{}' order by document-> '_id';


CALL documentdb_api.insert_bulk('db', '{"insert":"collection3", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}],"ordered":false}');
select document from documentdb_api.collection('db','collection3') where document @@ '{}' order by document-> '_id';


-- shard the collection by _id
select documentdb_api.shard_collection('db', 'collection4', '{"_id":"hashed"}', false);

CALL documentdb_api.insert_bulk('db', '{"insert":"collection4", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection4", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
select document from documentdb_api.collection('db','collection4') where document @@ '{}';


-- single-row insert with retry and auto-generated _id

CALL documentdb_api.insert_bulk('db', '{"insert":"collection5", "documents":[{"a":1}]}',NULL,'insert-2');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection5", "documents":[{"a":1}]}',NULL,'insert-2');
select count(*) from documentdb_api.collection('db','collection5') where document @@ '{}';

-- multi-row insert collection different shards
CALL documentdb_api.insert_bulk('db', '{"insert":"collection6", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','collection6') where document @@ '{}' order by document-> '_id';
select document from documentdb_api.collection('db','collection6') where document @@ '{"a":1}' order by document-> '_id';
select document from documentdb_api.collection('db','collection6') where document @@ '{"a":2}' order by document-> '_id';

-- insert with documents in special section
CALL documentdb_api.insert_bulk('db', '{"insert":"collection7"}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
select document from documentdb_api.collection('db','collection7') where document @@ '{}' order by document-> '_id';

-- insert with both docs specified.
CALL documentdb_api.insert_bulk('db', '{"insert":"collection9", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');

-- insert with id undefined skips
CALL documentdb_api.insert_bulk('db', '{"insert":"collection9", "documents":[{"_id":{ "$undefined": true } }]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"collection9"}', '{ "": [ {"_id":{ "$undefined": true } } ]}');


-- insert with invalid database
CALL documentdb_api.insert_bulk('Invalid Database Name', '{"insert":"notexists", "documents":[{"_id":1,"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"system.othercoll", "documents":[{"_id":1,"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"insert":"random$name", "documents":[{"_id":1,"a":1}]}');
CALL documentdb_api.insert_bulk('db', '{"insert":".randomname", "documents":[{"_id":1,"a":1}]}');
CALL documentdb_api.insert_bulk('verylongdatabasenameformeasuringthelimitsofdatabasenamesinmongodb', '{"insert":"coll", "documents":[{"_id":1,"a":1}]}');
CALL documentdb_api.insert_bulk('verylongdatabasenameformeasuringlimitsofdatabasenamesinmongoda', '{"insert":"verylongcollectionnameformeasuringthelimitsofcollectionnamesinmongodb", "documents":[{"_id":1,"a":1}]}');

-- when multiple batches and after multiple intermidate commits also things are fine
SET documentdb.batchWriteSubTransactionCount TO 2;
SELECT  documentdb_api.create_collection('commitDb', 'commitColl');

-- insert 10 documents
CALL documentdb_api.insert_bulk('commitDb', '{"insert":"commitColl", "documents":[{"_id":1}, {"_id":2}, {"_id":3},  {"_id":4},  {"_id":5}, {"_id":6}, {"_id":7}, {"_id":8}, {"_id":9}, {"_id":10}]}');

-- first one is duplicate and rest 9 are inserted
CALL documentdb_api.insert_bulk('commitDb', '{"insert":"commitColl", "documents":[{"_id":1}, {"_id":12}, {"_id":13},  {"_id":14},  {"_id":15}, {"_id":16}, {"_id":17}, {"_id":18}, {"_id":19}, {"_id":20}], "ordered":false}');

-- total doc count should be 10+9 
SELECT count(document) FROM documentdb_api.collection('commitDb','commitColl');


-- clean the collections
SELECT documentdb_api.drop_collection('db', 'collection0');
SELECT documentdb_api.drop_collection('db', 'collection1');
SELECT documentdb_api.drop_collection('db', 'collection2');
SELECT documentdb_api.drop_collection('db', 'collection3');
SELECT documentdb_api.drop_collection('db', 'collection4');
SELECT documentdb_api.drop_collection('db', 'collection5');
SELECT documentdb_api.drop_collection('db', 'collection6');
SELECT documentdb_api.drop_collection('db', 'collection7');
SELECT documentdb_api.drop_collection('db', 'collection8');