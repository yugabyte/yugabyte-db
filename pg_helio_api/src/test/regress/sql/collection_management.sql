SET search_path TO helio_api_catalog;
SET helio_api.next_collection_id TO 1900;
SET helio_api.next_collection_index_id TO 1900;

-- create a collection and insert a document
SELECT helio_api.create_collection('collection_management','originalname');
SELECT count(*) FROM helio_api.collection('collection_management','originalname');
SELECT helio_api.insert_one('collection_management','originalname','{"_id":"1", "a":1}');

-- query an existing collection
SELECT count(*) FROM helio_api.collection('collection_management','originalname');

-- query a non-existing collection
SELECT count(*) FROM helio_api.collection('collection_management','nonexistent');

-- SELECT * from a non-existing collection
SELECT * FROM helio_api.collection('collection_management','nonexistent');

-- EXPLAIN of querying a non-existing collection
EXPLAIN SELECT * FROM helio_api.collection('collection_management','nonexistent');

-- try to rename to an already existing name
-- SELECT helio_api.create_collection('collection_management','newname');
-- SELECT helio_api.rename_collection('collection_management','originalname', 'newname');

-- Disallow system.views, system.profile etc as the target collection names
-- SELECT helio_api.rename_collection('collection_management','originalname', 'system.views');
-- SELECT helio_api.rename_collection('collection_management','originalname', 'system.profile');

-- validate duplicate collections are not allowed
-- SELECT helio_api.create_collection('collection_management','collection1');
-- SELECT helio_api.create_collection('collection_management','collection1');

-- try to rename to an already existing name, after dropping the old one
-- SELECT helio_api.rename_collection('collection_management','originalname', 'newname', true);

-- try to query the original name
-- SELECT count(*) FROM helio_api.collection('collection_management','originalname');

-- try to query the new name
-- SELECT count(*) FROM helio_api.collection('collection_management','newname');

-- drop the collection
-- SELECT helio_api.drop_collection('collection_management','newname');
SELECT helio_api.drop_collection('collection_management','originalname');

-- try to drop a non-existent collection
SELECT helio_api.drop_collection('collection_management','doesnotexist');

-- recreate a table that previously existed
SELECT helio_api.create_collection('collection_management','originalname');
SELECT count(*) FROM helio_api.collection('collection_management','originalname');

SELECT helio_api_internal.create_indexes_non_concurrently('collection_management', '{"createIndexes": "drop_collection_test", "indexes": [{"key": {"a": 1}, "name": "my_idx_1"}]}');

-- store id of drop_collection_test before dropping it
SELECT collection_id AS drop_collection_test_id FROM helio_api_catalog.collections
WHERE collection_name = 'drop_collection_test' AND database_name = 'collection_management' \gset

-- Insert a record into index metadata that indicates an invalid collection index
-- to show that we delete records for invalid indexes too when dropping database.
INSERT INTO helio_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
VALUES (:drop_collection_test_id, 2020, ('invalid_index_2', '{"a": 1}', null, null, false, true, 2, 10), false);

-- drop the database
-- remove the drop collection below when uncommented
-- SELECT helio_api.drop_database('collection_management');
-- SELECT count(*) FROM helio_api_catalog.collections WHERE database_name = 'collection_management';

SELECT helio_api.drop_collection('collection_management', 'drop_collection_test');


SELECT COUNT(*)=0 FROM helio_api_catalog.collection_indexes
WHERE collection_id = :drop_collection_test_id;

SELECT helio_api.create_collection('collection_management','testDropViaUuid');
SELECT helio_api.create_collection('collection_management','testDropViaUuid2');
SELECT collection_uuid::text AS drop_collection_uuid2 FROM helio_api_catalog.collections WHERE database_name = 'collection_management' AND collection_name = 'testDropViaUuid2' \gset
SELECT collection_uuid::text AS drop_collection_uuid FROM helio_api_catalog.collections WHERE database_name = 'collection_management' AND collection_name = 'testDropViaUuid' \gset

SELECT helio_api.drop_collection('collection_management', 'testDropViaUuid', NULL, :'drop_collection_uuid2'::uuid);
SELECT helio_api.drop_collection('collection_management', 'testDropViaUuid', NULL, :'drop_collection_uuid'::uuid);

-- TODO: PREPARE/EXECUTE statements are failing with: ERROR: Query pipeline function must be in the FROM clause
-- needs looking into
-- try to target a collection via a prepared statement.
SELECT helio_api.create_collection('collection_management','testPrepared');
PREPARE collectionQuery1(text, text) AS SELECT document FROM helio_api.collection($1, $2);

EXECUTE collectionQuery1('collection_management', 'testPrepared');

-- try to run the prepared statement many times against a non-existent collection
PREPARE qcountNoneExistent(text, text, helio_core.bson, text) AS WITH "stage0" as ( SELECT document FROM helio_api.collection($1, $2) WHERE document OPERATOR(helio_api_catalog.@@) $3 ) ,
            "stage1" as ( SELECT helio_core.bson_repath_and_build($4, BSONSUM('{ "": 1 }'::helio_core.bson)) as document FROM "stage0" ) SELECT * FROM "stage1";

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
-- SET LOCAL helio_api.enableChangeStreams to false;
-- SELECT helio_api.create_collection('db_collection_management', 'change_feed_collection1');
-- SELECT helio_api.drop_collection('db_collection_management', 'change_feed_collection1');

-- change stream not supported yet
-- SELECT * FROM helio_data.changes WHERE change_stream_response @@ '{"ns": { "$eq": {"db": "db_collection_management", "coll": "change_feed_collection1"}}}';
-- END;

-- SELECT helio_api.create_collection('db_collection_management', 'change_feed_collection2');
-- SELECT helio_api.drop_collection('db_collection_management', 'change_feed_collection2');

-- SELECT change_stream_response->>'ns' as ns, change_stream_response->>'operationType' as operation_type FROM helio_data.changes WHERE change_stream_response @@ '{"ns": { "$eq": {"db": "db_collection_management", "coll": "change_feed_collection2"}}}' order by operation_type;
