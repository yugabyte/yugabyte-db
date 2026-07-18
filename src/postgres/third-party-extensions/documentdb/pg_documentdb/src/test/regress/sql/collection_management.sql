SET search_path TO documentdb_api_catalog;
SET documentdb.next_collection_id TO 1900;
SET documentdb.next_collection_index_id TO 1900;

-- create a collection and insert a document
SELECT documentdb_api.create_collection('collection_management','originalname');
SELECT count(*) FROM documentdb_api.collection('collection_management','originalname');
SELECT documentdb_api.insert_one('collection_management','originalname','{"_id":"1", "a":1}');

-- query an existing collection
SELECT count(*) FROM documentdb_api.collection('collection_management','originalname');

-- query a non-existing collection
SELECT count(*) FROM documentdb_api.collection('collection_management','nonexistent');

-- SELECT * from a non-existing collection
SELECT * FROM documentdb_api.collection('collection_management','nonexistent');

-- EXPLAIN of querying a non-existing collection
EXPLAIN SELECT * FROM documentdb_api.collection('collection_management','nonexistent');

-- try to rename to an already existing name
-- SELECT documentdb_api.create_collection('collection_management','newname');
-- SELECT documentdb_api.rename_collection('collection_management','originalname', 'newname');

-- Disallow system.views, system.profile etc as the target collection names
-- SELECT documentdb_api.rename_collection('collection_management','originalname', 'system.views');
-- SELECT documentdb_api.rename_collection('collection_management','originalname', 'system.profile');

-- validate duplicate collections are not allowed
-- SELECT documentdb_api.create_collection('collection_management','collection1');
-- SELECT documentdb_api.create_collection('collection_management','collection1');

-- try to rename to an already existing name, after dropping the old one
-- SELECT documentdb_api.rename_collection('collection_management','originalname', 'newname', true);

-- try to query the original name
-- SELECT count(*) FROM documentdb_api.collection('collection_management','originalname');

-- try to query the new name
-- SELECT count(*) FROM documentdb_api.collection('collection_management','newname');

-- drop the collection
-- SELECT documentdb_api.drop_collection('collection_management','newname');
SELECT documentdb_api.drop_collection('collection_management','originalname');

-- try to drop a non-existent collection
SELECT documentdb_api.drop_collection('collection_management','doesnotexist');

-- recreate a table that previously existed
SELECT documentdb_api.create_collection('collection_management','originalname');
SELECT count(*) FROM documentdb_api.collection('collection_management','originalname');

SELECT documentdb_api_internal.create_indexes_non_concurrently('collection_management', '{"createIndexes": "drop_collection_test", "indexes": [{"key": {"a": 1}, "name": "my_idx_1"}]}');

-- store id of drop_collection_test before dropping it
SELECT collection_id AS drop_collection_test_id FROM documentdb_api_catalog.collections
WHERE collection_name = 'drop_collection_test' AND database_name = 'collection_management' \gset

-- Insert a record into index metadata that indicates an invalid collection index
-- to show that we delete records for invalid indexes too when dropping database.
INSERT INTO documentdb_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
VALUES (:drop_collection_test_id, 2020, ('invalid_index_2', '{"a": 1}', null, null, false, true, 2, 10, null, null), false);

-- drop the database
SELECT documentdb_api.drop_database('collection_management');
SELECT count(*) FROM documentdb_api_catalog.collections WHERE database_name = 'collection_management';

SELECT COUNT(*)=0 FROM documentdb_api_catalog.collection_indexes
WHERE collection_id = :drop_collection_test_id;

SELECT documentdb_api.create_collection('collection_management','testDropViaUuid');
SELECT documentdb_api.create_collection('collection_management','testDropViaUuid2');
SELECT collection_uuid::text AS drop_collection_uuid2 FROM documentdb_api_catalog.collections WHERE database_name = 'collection_management' AND collection_name = 'testDropViaUuid2' \gset
SELECT collection_uuid::text AS drop_collection_uuid FROM documentdb_api_catalog.collections WHERE database_name = 'collection_management' AND collection_name = 'testDropViaUuid' \gset

SELECT documentdb_api.drop_collection('collection_management', 'testDropViaUuid', NULL, :'drop_collection_uuid2'::uuid);
SELECT documentdb_api.drop_collection('collection_management', 'testDropViaUuid', NULL, :'drop_collection_uuid'::uuid);

-- TODO: PREPARE/EXECUTE statements are failing with: ERROR: Query pipeline function must be in the FROM clause
-- needs looking into
-- try to target a collection via a prepared statement.
SELECT documentdb_api.create_collection('collection_management','testPrepared');
PREPARE collectionQuery1(text, text) AS SELECT document FROM documentdb_api.collection($1, $2);

EXECUTE collectionQuery1('collection_management', 'testPrepared');

-- try to run the prepared statement many times against a non-existent collection
PREPARE qcountNoneExistent(text, text, documentdb_core.bson, text) AS WITH "stage0" as ( SELECT document FROM documentdb_api.collection($1, $2) WHERE document OPERATOR(documentdb_api_catalog.@@) $3 ) ,
            "stage1" as ( SELECT documentdb_core.bson_repath_and_build($4, BSONSUM('{ "": 1 }'::documentdb_core.bson)) as document FROM "stage0" ) SELECT * FROM "stage1";

EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');
-- EXECUTE qcountNoneExistent('nonexistentdb', 'nonexistent', '{ "a": 1 }', 'count');

-- create/drop collection should honor GUC for change streams feature
-- change stream not supported yet
-- BEGIN;
-- SET LOCAL documentdb.enableChangeStreams to false;
-- SELECT documentdb_api.create_collection('db_collection_management', 'change_feed_collection1');
-- SELECT documentdb_api.drop_collection('db_collection_management', 'change_feed_collection1');

-- change stream not supported yet
-- SELECT * FROM documentdb_data.changes WHERE change_stream_response @@ '{"ns": { "$eq": {"db": "db_collection_management", "coll": "change_feed_collection1"}}}';
-- END;

-- SELECT documentdb_api.create_collection('db_collection_management', 'change_feed_collection2');
-- SELECT documentdb_api.drop_collection('db_collection_management', 'change_feed_collection2');

-- SELECT change_stream_response->>'ns' as ns, change_stream_response->>'operationType' as operation_type FROM documentdb_data.changes WHERE change_stream_response @@ '{"ns": { "$eq": {"db": "db_collection_management", "coll": "change_feed_collection2"}}}' order by operation_type;

-- validate collection name for empty
SELECT documentdb_api.create_collection('db', '');
SELECT documentdb_api.find_and_modify('', '{"findAndModify": "", "query": {"a": 1000}, "remove": 0.1, "sort": {"b": -1}}');

-- test documentdb_api.collection()
-- create/ insert into new collection
SELECT documentdb_api.insert_one('db','new_coll',' { "_id" :  1, "b" : 2 }', NULL);
\d+ documentdb_data.documents_1909;
SELECT * FROM documentdb_api.collection('db','new_coll');
SELECT document, shard_key_value FROM documentdb_api.collection('db','new_coll');

-- create/ insert into Lagacy collection
BEGIN;
SET LOCAL documentdb.enabledatatablewithoutcreationtime = off;
SELECT documentdb_api.insert_one('db','lagacy_coll',' { "_id" :  1, "b" : 2 }', NULL);
\d+ documentdb_data.documents_1910;
SELECT * FROM documentdb_api.collection('db','lagacy_coll');
SELECT document, shard_key_value FROM documentdb_api.collection('db','lagacy_coll');
ROLLBACK;

