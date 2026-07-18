SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 200;
SET documentdb.next_collection_index_id TO 200;

set documentdb.defaultUseCompositeOpClass to on;

-- create 2 single path indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_idb', '{ "createIndexes": "comp_index_selectivity", "indexes": [ { "name": "comp_index1", "key": { "path1": 1 } } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_idb', '{ "createIndexes": "comp_index_selectivity", "indexes": [ { "name": "comp_index2", "key": { "path2": 1 } } ] }', TRUE);

-- insert 5000 rows
SELECT COUNT(documentdb_api.insert_one('comp_idb', 'comp_index_selectivity', bson_build_document('_id'::text, i, 'path1'::text, i, 'path2'::text, i))) FROM generate_series(1, 5000) i;

ANALYZE documentdb_data.documents_201;

-- now do a query on both fields, where the selectivity of 1 is far less than the other
-- this should pick index for path1 (but doesn't)
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('comp_idb', '{ "find": "comp_index_selectivity", "filter": { "path1": 5, "path2": { "$gt": 500 } }}');

-- enable the composite planner GUC and now things should work (since documents are smaller than 1 KB)
set documentdb.enableCompositeIndexPlanner to on;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('comp_idb', '{ "find": "comp_index_selectivity", "filter": { "path1": 5, "path2": { "$gt": 500 } }}');

-- repeat this setup but with documents > 1 KB
TRUNCATE documentdb_data.documents_201;

SELECT COUNT(documentdb_api.insert_one('comp_idb', 'comp_index_selectivity',
    bson_build_document('_id'::text, i, 'path1'::text, i, 'path2'::text, i, 'large_text_field'::text, repeat('aaaaaaa', 500) ))) FROM generate_series(1, 5000) i;

ANALYZE documentdb_data.documents_201;
set documentdb.enableCompositeIndexPlanner to off;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('comp_idb', '{ "find": "comp_index_selectivity", "filter": { "path1": 5, "path2": { "$gt": 500 } }}');

set documentdb.enableCompositeIndexPlanner to on;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('comp_idb', '{ "find": "comp_index_selectivity", "filter": { "path1": 5, "path2": { "$gt": 500 } }}');