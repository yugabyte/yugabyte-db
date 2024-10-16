SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 6770000;
SET helio_api.next_collection_id TO 6770;
SET helio_api.next_collection_index_id TO 6770;


-- cannot create indexes for cosmos_search without the flag enabled.
SELECT helio_api.create_collection('db', 'create_indexes_text');

-- invalid scenarios
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": 1, "c": "text" } } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "hashed", "b": 1, "c": "text" } } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": "text" }, "name": "foo",  "expireAfterSeconds": 10 } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": "text" }, "name": "foo",  "unique": true } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c.$**": "text" }, "name": "foo" } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "foo", "wildcardProjection": { "a": 1 } } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "default_language": "ok" } ] }', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "weights": { "a": 2, "b": 3, "c": 4, "d": 5 } } ] }', true);

-- create a valid indexes
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM helio_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "a_text" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "b": "text", "c": 1 }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "b": "text", "c": "text" }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "idx1", "default_language": "es" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1", "weights": { "b": 100, "c": 200 } } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1", "weights": { "c": 200, "d": 10 } } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "$**": "text" }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text", "a": 1 }, "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "language_override": "idioma", "name": "idx1" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "default_language": "es" } ] }', true);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770

-- shard and re-observe
SELECT helio_api.shard_collection('db', 'create_indexes_text', '{ "_id" : "hashed" }', false);
SELECT helio_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d helio_data.documents_6770

CALL helio_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');


-- now create an index
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', true);

-- creating more text indexes should just fail.
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "b": "text" }, "name": "b_text" } ] }', true);

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "d": 1, "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "d": "text", "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "weights": { "a": 2, "b": 3, "c": 4, "d": 5 } } ] }', true);

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "f": "text" }, "name": "idx2", "default_language": "de" } ] }', true);
