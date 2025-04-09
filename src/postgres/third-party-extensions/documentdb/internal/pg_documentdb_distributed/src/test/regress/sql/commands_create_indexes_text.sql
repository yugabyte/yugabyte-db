SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 6770000;
SET documentdb.next_collection_id TO 6770;
SET documentdb.next_collection_index_id TO 6770;


-- cannot create indexes for cosmos_search without the flag enabled.
SELECT documentdb_api.create_collection('db', 'create_indexes_text');

-- invalid scenarios
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": 1, "c": "text" } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "hashed", "b": 1, "c": "text" } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": "text" }, "name": "foo",  "expireAfterSeconds": 10 } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": "text" }, "name": "foo",  "unique": true } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c.$**": "text" }, "name": "foo" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "foo", "wildcardProjection": { "a": 1 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "default_language": "ok" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "weights": { "a": 2, "b": 3, "c": 4, "d": 5 } } ] }', true);

-- create a valid indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "a_text" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "b": "text", "c": 1 }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "b": "text", "c": "text" }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "idx1", "default_language": "es" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1", "weights": { "b": 100, "c": 200 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text", "b": "text", "c": 1 }, "name": "idx1", "weights": { "c": 200, "d": 10 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "$**": "text" }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text", "a": 1 }, "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "language_override": "idioma", "name": "idx1" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

-- more tests with wildcard text indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "$**": "text", "a": "text" }, "name": "idx1" }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "a": "text", "$**": "text" }, "name": "idx1" }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "a": "text" }, "name": "idx1", "weights": { "$**": 1 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "a": "text" }, "name": "idx1", "weights": { "$**": 1, "b": 2 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "a": "text" }, "name": "idx1", "weights": { "b": 2, "$**": 1 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "a": "text", "$**": "text" }, "name": "idx1", "weights": { "b": 2 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "default_language": "es" } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

-- more tests with wildcard text indexes
-- this index spec corresponds to the index spec in the JS test: fts_blogwild.js
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "dummy": "text" }, "name": "idx1", "weights": { "$**": 1, "title": 2 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

-- test variations of the index spec - with the order of the weights, and with the wildcard
-- being specified in the key document
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "dummy": "text" }, "name": "idx1", "weights": { "title": 2, "$**": 1 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [{ "key": { "dummy": "text", "$**": "text" }, "name": "idx1", "weights": { "title": 2 } }]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');


-- we now create the same indexes as above, but with the metadata term `{ _fts: 'text', _ftsx: 1 }`
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1 }, "name": "a_text", "weights": { "a": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
SELECT * FROM documentdb_api_catalog.collection_indexes WHERE collection_id = 6770 ORDER BY 1,2,3;
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "a_text" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1, "c": 1 }, "name": "idx1", "weights": { "a": 1, "b": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "_fts": "text", "_ftsx": 1, "c": 1 }, "name": "idx1", "weights": { "b": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "_fts": "text", "_ftsx": 1 }, "name": "idx1", "weights": { "b": 1, "c": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1 }, "name": "idx1", "weights": { "$**": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1 }, "name": "idx1", "default_language": "es", "weights": { "a": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1, "c": 1 }, "name": "idx1", "weights": { "a": 1, "b": 100, "c": 200 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1, "c": 1 }, "name": "idx1", "weights": { "a": 1, "b": 1, "c": 200, "d": 10 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": 1, "_fts": "text", "_ftsx": 1 }, "name": "idx1", "weights": { "$**": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1, "a": 1 }, "name": "idx1", "weights": { "$**": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1 }, "language_override": "idioma", "name": "idx1", "weights": { "a": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "_fts": "text", "_ftsx": 1 }, "name": "idx1", "default_language": "es", "weights": { "$**": 1 } } ] }', true);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770


-- shard and re-observe
SELECT documentdb_api.shard_collection('db', 'create_indexes_text', '{ "_id" : "hashed" }', false);
SELECT documentdb_api.list_indexes_cursor_first_page('db', '{ "listIndexes": "create_indexes_text" }');
\d documentdb_data.documents_6770

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "create_indexes_text", "index": "idx1" }');


-- now create an index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', true);

-- creating more text indexes should just fail.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "b": "text" }, "name": "b_text" } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "d": 1, "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "c": 1, "d": "text", "b": "text" }, "name": "c_b_text" } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "weights": { "a": 2, "b": 3, "c": 4, "d": 5 } } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "create_indexes_text", "indexes": [ { "key": { "f": "text" }, "name": "idx2", "default_language": "de" } ] }', true);
