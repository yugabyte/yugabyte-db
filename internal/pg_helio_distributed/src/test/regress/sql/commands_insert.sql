SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 659000;
SET helio_api.next_collection_id TO 6590;
SET helio_api.next_collection_index_id TO 6590;

-- exercise invalid insert syntax errors
select helio_api.insert('db', NULL);
select helio_api.insert(NULL, '{"insert":"into", "documents":[{"a":1}]}');
select helio_api.insert('db', '{"documents":[{"a":1}]}');
select helio_api.insert('db', '{"insert":"into"}');
select helio_api.insert('db', '{"insert":["into"], "documents":[{"a":1}]}');
select helio_api.insert('db', '{"insert":"into", "documents":{"a":1}}');
select helio_api.insert('db', '{"insert":"into", "documents":[{"a":1}], "extra":1}');
select helio_api.insert('db', '{"insert":"into", "documents":[4]}');
select helio_api.insert('db', '{"insert":"into", "documents":[{"a":1}],"ordered":1}');

-- Disallow system.views, system.profile writes
select helio_api.insert('db', '{"insert":"system.views", "documents":[{"a":1}],"ordered":true}');
select helio_api.insert('db', '{"insert":"system.profile", "documents":[{"a":1}],"ordered":true}');

-- regular single-row insert
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');
select document from helio_api.collection('db','into') where document @@ '{}';

-- Insert into a db with same name and different case and collection being same. Expect to error
select helio_api.insert('dB', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');

-- Insert into a db with same name and different case and collection being different. Expect to error
select helio_api.insert('dB', '{"insert":"intonew", "documents":[{"_id":99,"a":99}]}');

-- Insert into same db and new collection.
select helio_api.insert('db', '{"insert":"intonew1", "documents":[{"_id":99,"a":99}]}');

-- keep the collection, but remove the rows
select helio_api.delete('db', '{"delete":"into", "deletes":[{"q":{},"limit":0}]}');

-- single-row insert with retry
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-1');
select document from helio_api.collection('db','into') where document @@ '{}';
rollback;

-- regular multi-row insert
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from helio_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- multi-row insert with first document key starts with $
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}]}');
select document from helio_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- multi-row insert with first document key starts with $ and ordered:false
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"$a":1},{"_id":2,"a":2}],"ordered":false}');
select document from helio_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- shard the collection by _id
select helio_api.shard_collection('db', 'into', '{"_id":"hashed"}', false);

-- single-row insert with retry
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}',NULL,'insert-2');
select document from helio_api.collection('db','into') where document @@ '{}';
rollback;

-- single-row insert with retry and auto-generated _id
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"a":1}]}',NULL,'insert-2');
select helio_api.insert('db', '{"insert":"into", "documents":[{"a":1}]}',NULL,'insert-2');
select count(*) from helio_api.collection('db','into') where document @@ '{}';
rollback;

-- multi-row insert into different shards
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}');
select document from helio_api.collection('db','into') where document @@ '{}' order by document-> '_id';
select document from helio_api.collection('db','into') where document @@ '{"a":1}' order by document-> '_id';
select document from helio_api.collection('db','into') where document @@ '{"a":2}' order by document-> '_id';
rollback;

-- insert with documents in special section
begin;
SELECT helio_api.insert('db', '{"insert":"into"}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
select document from helio_api.collection('db','into') where document @@ '{}' order by document-> '_id';
rollback;

-- insert with both docs specified.
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
rollback;

-- insert with id undefined skips
begin;
select helio_api.insert('db', '{"insert":"into", "documents":[{"_id":{ "$undefined": true } }]}');
rollback;

begin;
select helio_api.insert('db', '{"insert":"into"}', '{ "": [ {"_id":{ "$undefined": true } } ]}');
rollback;


-- single-row insert into non-existent collection when auto-creation is disabled
begin;
set local helio_api.enable_create_collection_on_insert to off;
select helio_api.insert('db', '{"insert":"notexists", "documents":[{"_id":1,"a":1}]}');
rollback;

-- insert with invalid database
begin;
select helio_api.insert('Invalid Database Name', '{"insert":"notexists", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select helio_api.insert('db', '{"insert":"system.othercoll", "documents":[{"_id":1,"a":1}]}');
rollback;


begin;
select helio_api.insert('db', '{"insert":"random$name", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select helio_api.insert('db', '{"insert":".randomname", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select helio_api.insert('verylongdatabasenameformeasuringthelimitsofdatabasenamesinmongodb', '{"insert":"coll", "documents":[{"_id":1,"a":1}]}');
rollback;

begin;
select helio_api.insert('verylongdatabasenameformeasuringlimitsofdatabasenamesinmongodb', '{"insert":"verylongcollectionnameformeasuringthelimitsofcollectionnamesinmongodb", "documents":[{"_id":1,"a":1}]}');
rollback;

select helio_api.drop_collection('db','into');
select helio_api.drop_collection('db','intonew1');
