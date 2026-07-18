SET search_path TO documentdb_api_catalog;
SET documentdb.next_collection_id TO 8700;
SET documentdb.next_collection_index_id TO 8700;

SELECT documentdb_api.create_collection('collmod','coll_mod_test_hidden');
SELECT COUNT(documentdb_api.insert_one('collmod','coll_mod_test_hidden', FORMAT('{"_id":"%s", "a": %s }', i, i )::documentdb_core.bson)) FROM generate_series(1, 100) i;

-- cannot create an index as hidden
SELECT documentdb_api_internal.create_indexes_non_concurrently('collmod', '{"createIndexes": "coll_mod_test_hidden", "indexes": [{"key": {"a": 1}, "name": "my_idx_1", "hidden": true  }]}');

SELECT documentdb_api_internal.create_indexes_non_concurrently('collmod', '{"createIndexes": "coll_mod_test_hidden", "indexes": [{"key": {"a": 1}, "name": "my_idx_1" }]}', TRUE);

\d documentdb_data.documents_8701
ANALYZE documentdb_data.documents_8701;

-- get list index output
SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

-- the index is used for queries
set enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');

-- now hide the index
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "my_idx_1", "hidden": true } }');

-- print the status
\d documentdb_data.documents_8701

SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

-- the index is not used for queries
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');

-- cannot hide the primary key index (since it's unique)
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "_id_", "hidden": true } }');

-- now inserts done while the index is hidden do get factored into the final results.
SELECT documentdb_api.insert_one('collmod','coll_mod_test_hidden', '{"_id":"101", "a": 101 }'::documentdb_core.bson);
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 101 } }');

-- unhide the index
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "my_idx_1", "hidden": false } }');

-- print the status: index is no longer invalid
\d documentdb_data.documents_8701

-- hidden is no longer in the options
SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

-- can use the index again
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');

-- the row shows up from the index 
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 101 } }');