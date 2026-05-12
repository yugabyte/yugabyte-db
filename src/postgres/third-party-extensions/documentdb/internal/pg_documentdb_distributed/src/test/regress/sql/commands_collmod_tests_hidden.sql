SET search_path TO documentdb_api_catalog;
SET citus.next_shard_id TO 920000;
SET documentdb.next_collection_id TO 9200;
SET documentdb.next_collection_index_id TO 9200;

SELECT documentdb_api.create_collection('collmod','coll_mod_test_hidden');
SELECT COUNT(documentdb_api.insert_one('collmod','coll_mod_test_hidden', FORMAT('{"_id":"%s", "a": %s }', i, i )::documentdb_core.bson)) FROM generate_series(1, 100) i;

-- cannot create an index as hidden
SELECT documentdb_api_internal.create_indexes_non_concurrently('collmod', '{"createIndexes": "coll_mod_test_hidden", "indexes": [{"key": {"a": 1}, "name": "my_idx_1", "hidden": true  }]}');

SELECT documentdb_api_internal.create_indexes_non_concurrently('collmod', '{"createIndexes": "coll_mod_test_hidden", "indexes": [{"key": {"a": 1}, "name": "my_idx_1" }]}', TRUE);

set citus.show_shards_for_app_name_prefixes to '*';
\d documentdb_data.documents_9201
\d documentdb_data.documents_9201_920002
ANALYZE documentdb_data.documents_9201;

-- get list index output
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

-- the index is used for queries
set enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');

-- now hide the index
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "my_idx_1", "hidden": true } }');

-- print the status
\d documentdb_data.documents_9201
\d documentdb_data.documents_9201_920002

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

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
\d documentdb_data.documents_9201
\d documentdb_data.documents_9201_920002

-- hidden is no longer in the options
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

-- can use the index again
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');

-- the row shows up from the index 
SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 101 } }');

-- shard the collection.
SELECT documentdb_api.shard_collection('{ "shardCollection": "collmod.coll_mod_test_hidden", "key": { "_id": "hashed" }, "numInitialChunks": 3 }');

set citus.explain_all_tasks to on;
BEGIN;
set local enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
ROLLBACK;

-- now hide the index again (with debug logs)
set client_min_messages to debug1;
SET citus.multi_shard_modify_mode TO 'sequential';
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "my_idx_1", "hidden": true } }');
reset client_min_messages;
reset citus.multi_shard_modify_mode;
\d documentdb_data.documents_9201
\d documentdb_data.documents_9201_920003
\d documentdb_data.documents_9201_920004
\d documentdb_data.documents_9201_920005

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

BEGIN;
set local enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
ROLLBACK;

-- unhide the index again
SELECT documentdb_api.coll_mod('collmod', 'coll_mod_test_hidden', '{ "collMod": "coll_mod_test_hidden", "index": { "name": "my_idx_1", "hidden": false } }');
\d documentdb_data.documents_9201
\d documentdb_data.documents_9201_920003
\d documentdb_data.documents_9201_920004
\d documentdb_data.documents_9201_920005

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('collmod', '{ "listIndexes": "coll_mod_test_hidden" }');

BEGIN;
set local enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('collmod', '{ "find": "coll_mod_test_hidden", "filter": { "a": 1 } }');
ROLLBACK;
