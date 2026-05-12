SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 700;
SET documentdb.next_collection_index_id TO 700;

set documentdb.defaultUseCompositeOpClass to on;

-- first create with unique hash off.
set documentdb.enableCompositeUniqueHash to off;
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{ "createIndexes": "uniqueColl", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "unique": true } ]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{ "createIndexes": "uniqueColl", "indexes": [ { "key": { "b": 1, "c": 1 }, "name": "b_c_1", "unique": true } ]}', TRUE);

\d documentdb_data.documents_701

SELECT COUNT(documentdb_api.insert_one('uniquedb', 'uniqueColl', bson_build_document('_id'::text, i, 'a'::text, i, 'b'::text, i, 'c'::text, i))) FROM generate_series(1, 10) i;

VACUUM (FREEZE ON, INDEX_CLEANUP ON) documentdb_data.documents_701;

-- now query the layout of both indexes
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_702', 0));
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_703', 0));

SELECT i, (entry->>'offset')::int8, (entry->>'attrNumber')::int8, (entry->>'firstEntry') FROM generate_series(1, 1) i JOIN LATERAL (
    SELECT entry FROM documentdb_api_internal.documentdb_rum_page_get_entries(public.get_raw_page('documentdb_data.documents_rum_index_703', i), 'documentdb_data.documents_rum_index_703'::regclass) entry) right_query ON TRUE
ORDER BY i, (entry->>'offset')::int8;

SELECT i, (entry->>'offset')::int8, (entry->>'attrNumber')::int8, (entry->>'firstEntry') FROM generate_series(1, 1) i JOIN LATERAL (
    SELECT entry FROM documentdb_api_internal.documentdb_rum_page_get_entries(public.get_raw_page('documentdb_data.documents_rum_index_702', i), 'documentdb_data.documents_rum_index_702'::regclass) entry) right_query ON TRUE
ORDER BY i, (entry->>'offset')::int8;

-- recheck with composite unique hash
set documentdb.enableCompositeUniqueHash to on;
CALL documentdb_api.drop_indexes('uniquedb', '{ "dropIndexes": "uniqueColl", "index": [ "a_1", "b_c_1" ]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{ "createIndexes": "uniqueColl", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "unique": true } ]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{ "createIndexes": "uniqueColl", "indexes": [ { "key": { "b": 1, "c": 1 }, "name": "b_c_1", "unique": true } ]}', TRUE);

-- now query the layout of both indexes
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_704', 0));
SELECT documentdb_api_internal.documentdb_rum_get_meta_page_info(public.get_raw_page('documentdb_data.documents_rum_index_705', 0));

SELECT i, (entry->>'offset')::int8, (entry->>'attrNumber')::int8, (entry->>'firstEntry') FROM generate_series(1, 1) i JOIN LATERAL (
    SELECT entry FROM documentdb_api_internal.documentdb_rum_page_get_entries(public.get_raw_page('documentdb_data.documents_rum_index_704', i), 'documentdb_data.documents_rum_index_704'::regclass) entry) right_query ON TRUE
ORDER BY i, (entry->>'offset')::int8;

SELECT i, (entry->>'offset')::int8, (entry->>'attrNumber')::int8, (entry->>'firstEntry') FROM generate_series(1, 1) i JOIN LATERAL (
    SELECT entry FROM documentdb_api_internal.documentdb_rum_page_get_entries(public.get_raw_page('documentdb_data.documents_rum_index_705', i), 'documentdb_data.documents_rum_index_705'::regclass) entry) right_query ON TRUE
ORDER BY i, (entry->>'offset')::int8;
