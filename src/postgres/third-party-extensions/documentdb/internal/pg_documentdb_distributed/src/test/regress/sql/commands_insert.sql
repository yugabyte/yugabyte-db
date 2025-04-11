SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 659000;
SET documentdb.next_collection_id TO 6590;
SET documentdb.next_collection_index_id TO 6590;

-- exercise invalid insert syntax errors
select documentdb_api.insert('db', NULL);
select documentdb_api.insert(NULL, '{"insert":"into", "documents":[{"a":1}]}');
select documentdb_api.insert('db', '{"documents":[{"a":1}]}');
select documentdb_api.insert('db', '{"insert":"into"}');
select documentdb_api.insert('db', '{"insert":["into"], "documents":[{"a":1}]}');
select documentdb_api.insert('db', '{"insert":"into", "documents":{"a":1}}');
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}], "extra":1}');
select documentdb_api.insert('db', '{"insert":"into", "documents":[4]}');
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}],"ordered":1}');

-- Disallow system.views, system.profile writes
select documentdb_api.insert('db', '{"insert":"system.views", "documents":[{"a":1}],"ordered":true}');
select documentdb_api.insert('db', '{"insert":"system.profile", "documents":[{"a":1}],"ordered":true}');

-- regular single-row insert
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');
select document from documentdb_api.collection('db','into') where document @@ '{}';

-- Insert into a db with same name and different case and collection being same. Expect to error
select documentdb_api.insert('dB', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');

-- Insert into a db with same name and different case and collection being different. Expect to error
select documentdb_api.insert('dB', '{"insert":"intonew", "documents":[{"_id":99,"a":99}]}');

-- Insert into same db and new collection.
select documentdb_api.insert('db', '{"insert":"intonew1", "documents":[{"_id":99,"a":99}]}');

-- keep the collection, but remove the rows
select documentdb_api.delete('db', '{"delete":"into", "deletes":[{"q":{},"limit":0}]}');

-- single-row insert with retry
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
select document from documentdb_api.collection('db','into') where document @@ '{}';
rollback;

-- regular multi-row insert
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- multi-row insert with first document key starts with $
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- multi-row insert with first document key starts with $ and ordered:false
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}],"ordered":false}');
select document from documentdb_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- shard the collection by _id
select documentdb_api.shard_collection('db', 'into', '{"_id":"hashed"}', false);

-- single-row insert with retry
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
select document from documentdb_api.collection('db','into') where document @@ '{}';
rollback;

-- single-row insert with retry and auto-generated _id
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}]}',NULL,'insert-2');
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}]}',NULL,'insert-2');
select count(*) from documentdb_api.collection('db','into') where document @@ '{}';
rollback;

-- multi-row insert into different shards
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from documentdb_api.collection('db','into') where document @@ '{}' order by document-> '_id';
select document from documentdb_api.collection('db','into') where document @@ '{"a":1}' order by document-> '_id';
select document from documentdb_api.collection('db','into') where document @@ '{"a":2}' order by document-> '_id';
rollback;

-- insert with documents in special section
begin;
SELECT documentdb_api.insert('db', '{"insert":"into"}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
select document from documentdb_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- insert with both docs specified.
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
rollback;

-- insert with id undefined skips
begin;
select documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":{ "$undefined": true } }]}');
rollback;

begin;
select documentdb_api.insert('db', '{"insert":"into"}', '{ "": [ {"_id":{ "$undefined": true } } ]}');
rollback;


-- single-row insert into non-existent collection when auto-creation is disabled
begin;
set local documentdb.enable_create_collection_on_insert to off;
select documentdb_api.insert('db', '{"insert":"notexists", "documents":[{"_id":1,"a":1}]}');
rollback;

-- insert with invalid database
begin;
select documentdb_api.insert('Invalid Database Name', '{"insert":"notexists", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select documentdb_api.insert('db', '{"insert":"system.othercoll", "documents":[{"_id":1,"a":1}]}');
rollback;


begin;
select documentdb_api.insert('db', '{"insert":"random$name", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select documentdb_api.insert('db', '{"insert":".randomname", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select documentdb_api.insert('verylongdatabasenameformeasuringthelimitsofdatabasenamesinmongodb', '{"insert":"coll", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select documentdb_api.insert('verylongdatabasenameformeasuringlimitsofdatabasenamesinmongodb', '{"insert":"verylongcollectionnameformeasuringthelimitsofcollectionnamesinmongodb", "documents":[{"_id":1,"a":1}]}');
rollback;

select documentdb_api.drop_collection('db','into');
select documentdb_api.drop_collection('db','intonew1');
