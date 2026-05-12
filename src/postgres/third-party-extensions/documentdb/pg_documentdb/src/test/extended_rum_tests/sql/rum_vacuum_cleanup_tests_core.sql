
SELECT name, setting, reset_val, boot_val FROM pg_settings WHERE name in ('documentdb_rum.track_incomplete_split', 'documentdb_rum.fix_incomplete_split');

SELECT documentdb_api.drop_collection('pvacuum_db', 'pclean');
SELECT documentdb_api.create_collection('pvacuum_db', 'pclean');

SELECT collection_id AS vacuum_col FROM documentdb_api_catalog.collections WHERE database_name = 'pvacuum_db' AND collection_name = 'pclean' \gset

-- disable autovacuum to have predicatability
SELECT FORMAT('ALTER TABLE documentdb_data.documents_%s set (autovacuum_enabled = off)', :vacuum_col) \gexec


SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 1000) AS i;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'pvacuum_db',
    '{ "createIndexes": "pclean", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableCompositeTerm": true } ] }', TRUE);

SELECT index_id AS vacuum_index_id FROM documentdb_api_catalog.collection_indexes WHERE collection_id = :vacuum_col AND index_id != :vacuum_col \gset

-- use the index - 
set documentdb_rum.vacuum_cleanup_entries to off;
set documentdb.enableExtendedExplainPlans to on;
set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

-- drop all the rows now
reset documentdb.forceDisableSeqScan;
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$gte": 10 } }, "limit": 0 } ]}');
set documentdb.forceDisableSeqScan to on;

-- query again (should return 10 rows with 1000 loops)
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

-- vacuum the collection
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec


-- query again (should return 10 rows but still with 1000 loops since we don't clean entries).
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

reset documentdb.forceDisableSeqScan;
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1001, 2000) AS i;
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$gte": 1010 } }, "limit": 0 } ]}');
set documentdb.forceDisableSeqScan to on;

-- now set the guc to clean the entries
set documentdb_rum.vacuum_cleanup_entries to on;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

-- repeat one more time
reset documentdb.forceDisableSeqScan;
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(2001, 3000) AS i;
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$gte": 2010 } }, "limit": 0 } ]}');
set documentdb.forceDisableSeqScan to on;

-- now set the guc to clean the entries
set documentdb_rum.vacuum_cleanup_entries to on;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

reset documentdb.forceDisableSeqScan;

-- insert some entries to create posting trees.
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": -%s, "a": 500 }', i)::bson)) FROM generate_series(1, 3000) AS i;

-- now delete everything includig posting tree entries.
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$lt": 2000 } }, "limit": 0 } ]}');

set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

-- now set the guc to clean up entry pages
set documentdb_rum.prune_rum_empty_pages to on;
set client_min_messages to DEBUG1;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
reset client_min_messages;

-- delete one more row to ensure vacuum has a chance to clean up
reset documentdb.forceDisableSeqScan;
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$lte": 2000 } }, "limit": 0 } ]}');
set client_min_messages to DEBUG1;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
reset client_min_messages;

set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);

-- introduce dead pages and use the repair functions to clean up the index.
reset documentdb.forceDisableSeqScan;
SELECT FORMAT('TRUNCATE documentdb_data.documents_%s;', :vacuum_col) \gexec

-- insert 3000 docs
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 3000) AS i;

set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);
reset documentdb.forceDisableSeqScan;

-- delete 3000 docs
set documentdb_rum.vacuum_cleanup_entries to off;
set documentdb_rum.prune_rum_empty_pages to off;
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$exists": true } }, "limit": 0 } ]}');
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec

-- we should have a lot of empty pages
set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);
reset documentdb.forceDisableSeqScan;

-- call the repair function.
set documentdb_rum.vacuum_cleanup_entries to on;
SELECT documentdb_api_internal.rum_prune_empty_entries_on_index(('documentdb_data.documents_rum_index_' || :vacuum_index_id)::regclass);

-- should have fewer entries due to pruning.
set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);
reset documentdb.forceDisableSeqScan;

set documentdb_rum.prune_rum_empty_pages to on;
SELECT documentdb_api_internal.rum_prune_empty_entries_on_index(('documentdb_data.documents_rum_index_' || :vacuum_index_id)::regclass);

set documentdb.forceDisableSeqScan to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$ EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE OFF, BUFFERS OFF, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_count('pvacuum_db', '{ "count": "pclean", "query": { "a": { "$exists": true } } }') $cmd$);
reset documentdb.forceDisableSeqScan;

-- test multi-level posting tree cleanup
SELECT FORMAT('TRUNCATE documentdb_data.documents_%s;', :vacuum_col) \gexec

-- insert some entries
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 1000) AS i;

-- now insert entries that trigger a multi-level posting tree (allow only 50 entries per data page)
set documentdb_rum.data_page_posting_tree_size = 3;
SELECT COUNT(documentdb_api.insert_one('pvacuum_db', 'pclean',  FORMAT('{ "_id": -%s, "a": 500 }', i)::bson)) FROM generate_series(1, 15000) AS i;

-- now assert that it produces a multi-level tree
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page(('documentdb_data.documents_rum_index_' || :vacuum_index_id), 0));

WITH r1 AS (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page(('documentdb_data.documents_rum_index_' || :vacuum_index_id), i)) AS entry FROM generate_series(1, 21) i)
SELECT * FROM r1 WHERE entry->>'flagsStr' LIKE '%DATA%' ORDER by (entry->>'flags')::int8, i ASC;

-- now delete everything.
SELECT documentdb_api.delete('pvacuum_db', '{ "delete": "pclean", "deletes": [ { "q": { "_id": { "$exists": true } }, "limit": 0 } ]}');
set client_min_messages to LOG;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec

WITH r1 AS (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page(('documentdb_data.documents_rum_index_' || :vacuum_index_id), i)) AS entry FROM generate_series(1, 21) i)
SELECT * FROM r1 WHERE entry->>'flagsStr' LIKE '%DATA%' ORDER by (entry->>'flags')::int8, i ASC;
