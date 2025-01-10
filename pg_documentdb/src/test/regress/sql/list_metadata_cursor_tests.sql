SET search_path TO helio_core, helio_api, public;
SET helio_api.next_collection_id TO 9100;
SET helio_api.next_collection_index_id TO 9100;

-- create a collection in db1
SELECT helio_api.create_collection('list_metadata_db1', 'list_metadata_coll1');

UPDATE helio_api_catalog.collections SET collection_uuid = NULL WHERE database_name = 'list_metadata_db1';
SELECT cursorpage, continuation, persistconnection, cursorid  FROM helio_api.list_collections_cursor_first_page('list_metadata_db1', '{ "listCollections": 1, "nameOnly": true }');

-- create a sharded collection in db1
SELECT helio_api.create_collection('list_metadata_db1', 'list_metadata_coll2');
SELECT helio_api.shard_collection('list_metadata_db1', 'list_metadata_coll2', '{ "_id": "hashed" }', false);

-- create 2 collection in db2
SELECT helio_api.create_collection('list_metadata_db2', 'list_metadata_db2_coll1');
SELECT helio_api.create_collection('list_metadata_db2', 'list_metadata_db2_coll2');

-- create 2 views (one for db1 and one for db2)
SELECT helio_api.create_collection_view('list_metadata_db1', '{ "create": "list_metadata_view1_1", "viewOn": "list_metadata_coll1", "pipeline": [{ "$limit": 100 }] }');
SELECT helio_api.create_collection_view('list_metadata_db2', '{ "create": "list_metadata_view2_1", "viewOn": "list_metadata_coll2", "pipeline": [{ "$skip": 100 }] }');

-- reset collection_uuids
UPDATE helio_api_catalog.collections SET collection_uuid = NULL WHERE database_name = 'list_metadata_db1';
UPDATE helio_api_catalog.collections SET collection_uuid = NULL WHERE database_name = 'list_metadata_db2';

SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_collections_cursor_first_page('list_metadata_db1', '{ "listCollections": 1 }') ORDER BY 1;

SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_collections_cursor_first_page('list_metadata_db2', '{ "listCollections": 1, "nameOnly": true }') ORDER BY 1;
SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_collections_cursor_first_page('list_metadata_db2', '{ "listCollections": 1 }') ORDER BY 1;

SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_collections_cursor_first_page('list_metadata_db1', '{ "listCollections": 1, "filter": { "type": "view" } }') ORDER BY 1;
SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_collections_cursor_first_page('list_metadata_db1', '{ "listCollections": 1, "filter": { "info.readOnly": false } }') ORDER BY 1;

-- create some indexes for the collections in db1
SELECT helio_api_internal.create_indexes_non_concurrently('list_metadata_db1', '{ "createIndexes": "list_metadata_coll1", "indexes": [ { "key": { "a": 1 }, "name": "a_1" }, { "key": { "b.$**": 1 }, "name": "b_1"} ]}', TRUE);

SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('list_metadata_db1', '{ "listIndexes": "list_metadata_coll1" }') ORDER BY 1;
SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('list_metadata_db1', '{ "listIndexes": "list_metadata_coll2" }') ORDER BY 1;

-- fails
SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('list_metadata_db1', '{ "listIndexes": "list_metadata_view1_1" }') ORDER BY 1;
SELECT helio_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('list_metadata_db1', '{ "listIndexes": "list_metadata_non_existent" }') ORDER BY 1;