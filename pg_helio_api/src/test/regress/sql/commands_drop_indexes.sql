SET search_path TO helio_api, helio_api_catalog,helio_core;
SET helio_api.next_collection_id TO 15000;
SET helio_api.next_collection_index_id TO 15000;

---- dropIndexes - top level - parse error ----
SELECT helio_api.create_collection('db', 'collection_3');

CALL helio_api.drop_indexes('db', NULL);
CALL helio_api.drop_indexes(NULL, '{}');
CALL helio_api.drop_indexes('db', '{}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": null, "index": ["my_idx_1", "does_not_exist"]}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "unknown_field": 1}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": null}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3"}');

-- this is ok
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index":[]}');

CALL helio_api.drop_indexes('db', '{"dropIndexes": 1, "index":[]}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": 1}');

---- dropIndexes - top level - not implemented yet ----
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": {}}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "writeConcern": 1}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "comment": 1}');

---- dropIndexes -- collection doesn't exist ----
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_4"}');

---- dropIndexes -- index doesn't exist ----
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_3", "indexes": [{"key": {"a": 1}, "name": "my_idx_1"}]}', true);
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": ["my_idx_1", "does_not_exist"]}');
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": ["does_not_exist", "my_idx_1"]}');

---- dropIndexes -- unique unidex should work (unique index alters the table to remove a constraint)
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_3", "indexes": [{"key": {"a": 1}, "name": "idx_1", "unique": true }]}', true);
CALL helio_api.drop_indexes('db', '{"dropIndexes": "collection_3", "index": ["idx_1"]}');

-- test drop_collection

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "drop_collection_test", "indexes": [{"key": {"a": 1}, "name": "my_idx_1"}]}');

-- store id of drop_collection_test before dropping it
SELECT collection_id AS db_drop_collection_test_id FROM helio_api_catalog.collections
WHERE collection_name = 'drop_collection_test' AND database_name = 'db' \gset

-- Insert a record into index metadata that indicates an invalid collection index
-- to show that we delete records for invalid indexes too when dropping collection.
INSERT INTO helio_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
VALUES (:db_drop_collection_test_id, 1010, ('invalid_index_1', '{"a": 1}', null, null, null, null, 2, null), false);

SELECT helio_api.drop_collection('db', 'drop_collection_test');

SELECT COUNT(*)=0 FROM helio_api_catalog.collection_indexes
WHERE collection_id = :db_drop_collection_test_id;
