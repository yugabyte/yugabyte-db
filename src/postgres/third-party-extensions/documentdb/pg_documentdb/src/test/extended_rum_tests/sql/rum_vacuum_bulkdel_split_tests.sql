SET search_path TO documentdb_api_catalog, documentdb_core, public;
SET documentdb.next_collection_id TO 800;
SET documentdb.next_collection_index_id TO 800;

set documentdb_rum.prune_rum_empty_pages to on;
set documentdb_rum.enable_new_bulk_delete to on;
set documentdb_rum.enable_new_bulk_delete_inline_data_pages to on;
set documentdb_rum.vacuum_cleanup_entries to on;

SELECT documentdb_api.drop_collection('pvacuum_split_db', 'pbulkdel');
SELECT documentdb_api.create_collection('pvacuum_split_db', 'pbulkdel');

SELECT collection_id AS vacuum_col FROM documentdb_api_catalog.collections WHERE database_name = 'pvacuum_split_db' AND collection_name = 'pbulkdel' \gset

-- disable autovacuum to have predicatability
SELECT FORMAT('ALTER TABLE documentdb_data.documents_%s set (autovacuum_enabled = off)', :vacuum_col) \gexec

-- insert 500 rows (generates many entry pages)
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 500) AS i;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'pvacuum_split_db',
    '{ "createIndexes": "pbulkdel", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableCompositeTerm": true } ] }', TRUE);

SELECT index_id AS vacuum_index_id FROM documentdb_api_catalog.collection_indexes WHERE collection_id = :vacuum_col AND index_id != :vacuum_col \gset

-- print stats per page
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_802', 0));

-- insert many tuples at the end (this splits it to the higher pages)
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(501, 2500) AS i;
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(2501, 5000) AS i;

-- vacuum the collection
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec

-- print stats per page
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_802', 0));

-- now delete the earlier rows in the tree (occupying the lower pages)
SELECT documentdb_api.delete('pvacuum_split_db', '{ "delete": "pbulkdel", "deletes": [ { "q": { "_id": { "$lte": 3000 } }, "limit": 0 } ]}');

-- vacuum the collection (twice to ensure we have void pages): but make sure to skip the visibility check (to ensure we get void pages)
set documentdb_rum.skip_global_visibility_check_on_prune to on;
set client_min_messages to LOG;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
reset client_min_messages;

-- print stats per page: Note that the min/max dead pages that are void are less than 36.
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_802', 0));
SELECT MIN(i), MAX(i), COUNT(*) FROM (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page('documentdb_data.documents_rum_index_802', i)) entry FROM generate_series(1, 35) i) AS q1
    WHERE entry->>'flagsStr' LIKE '%HALFDEAD%' OR entry->>'flagsStr' LIKE '%DELETED%';

-- now that there's void pages, insert many docs to induce page splits (and that should reuse earlier pages).
-- do this wiht a fixed cycleId (that we can later reuse for vacuum)
set documentdb_rum.vacuum_cycle_id_override to 917;
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s.5, "a": %s.5 }', i, i)::bson)) FROM generate_series(3001, 5000) AS i;
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s.6, "a": %s.6 }', i, i)::bson)) FROM generate_series(3001, 5000) AS i;
SELECT COUNT(documentdb_api.insert_one('pvacuum_split_db', 'pbulkdel',  FORMAT('{ "_id": %s.3, "a": %s.3 }', i, i)::bson)) FROM generate_series(3001, 5000) AS i;

-- should have no deleted/dead pages (all reused)
SELECT MIN(i), MAX(i), COUNT(*) FROM (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page('documentdb_data.documents_rum_index_802', i)) entry FROM generate_series(1, 56) i) AS q1
    WHERE entry->>'flagsStr' LIKE '%HALFDEAD%' OR entry->>'flagsStr' LIKE '%DELETED%';

-- at least some pages should have a right link less than the current page
SELECT COUNT(*) FROM (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page('documentdb_data.documents_rum_index_802', i)) entry FROM generate_series(1, 56) i) AS q1
    WHERE (entry->>'rightLink')::int4 < i;

-- the new pages should have the appropriate vacuum cycleId
SELECT COUNT(*), MIN((entry->>'cycleId')::int4), MAX((entry->>'cycleId')::int4) FROM (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page('documentdb_data.documents_rum_index_802', i)) entry FROM generate_series(1, 56) i) AS q1
    WHERE (entry->>'cycleId')::int4 > 0;

-- delete everything
SELECT documentdb_api.delete('pvacuum_split_db', '{ "delete": "pbulkdel", "deletes": [ { "q": { }, "limit": 0 } ]}');

-- run a vacuum in the mode that only works on backtrack mode.
set documentdb_rum.default_traverse_rum_page_only_on_backtrack to on;
set client_min_messages to LOG;
SELECT FORMAT('VACUUM (FREEZE ON, INDEX_CLEANUP ON, DISABLE_PAGE_SKIPPING ON) documentdb_data.documents_%s;', :vacuum_col) \gexec
reset client_min_messages;

-- should have no cycleid
SELECT COUNT(*), MIN((entry->>'cycleId')::int4), MAX((entry->>'cycleId')::int4) FROM (SELECT i, documentdb_api_internal.documentdb_rum_page_get_stats(public.get_raw_page('documentdb_data.documents_rum_index_802', i)) entry FROM generate_series(1, 56) i) AS q1
    WHERE (entry->>'cycleId')::int4 > 0;

-- check the stats (should have 2 entries - one for the leftmost and rightmost).
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_802', 0));