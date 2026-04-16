SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SELECT documentdb_api.drop_collection('lookupdb', 'planes'), documentdb_api.drop_collection('lookupdb', 'gate_availability');

-- Insert data
SELECT documentdb_api.insert_one('lookupdb','planes',' { "_id" : 1, "model" : "A380", "price" : 280, "quantity" : 20 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','planes','{ "_id" : 2, "model" : "A340", "price" : 140, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','planes',' { "_id" : 3, "model" : "A330", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','planes',' { "_id" : 4, "model" : "737", "price" : 50, "quantity" : 30 }', NULL);

SELECT documentdb_api.insert_one('lookupdb','gate_availability',' { "_id" : 1, "plane_model" : "A330", "gates" : 30 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','gate_availability',' { "_id" : 11, "plane_model" : "A340", "gates" : 10 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','gate_availability','{ "_id" : 2, "plane_model" : "A380", "gates" : 5 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','gate_availability','{ "_id" : 3, "plane_model" : "A350", "gates" : 20 }', NULL);
SELECT documentdb_api.insert_one('lookupdb','gate_availability','{ "_id" : 4, "plane_model" : "737", "gates" : 110 }', NULL);


-- set up indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('lookupdb', '{ "createIndexes": "planes", "indexes": [ { "key": { "model": 1 }, "name": "planes_model_1" } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('lookupdb', '{ "createIndexes": "gate_availability", "indexes": [ { "key": { "plane_model": 1 }, "name": "plane_model_1" } ] }', TRUE);

-- Remove primary key
SELECT collection_id AS planes_id FROM documentdb_api_catalog.collections WHERE database_name = 'lookupdb' AND collection_name = 'planes' \gset
SELECT collection_id AS gate_availability_id FROM documentdb_api_catalog.collections WHERE database_name = 'lookupdb' AND collection_name = 'gate_availability' \gset


SELECT 'ALTER TABLE documentdb_data.documents_' || :'planes_id' || ' DROP CONSTRAINT collection_pk_' || :'planes_id' \gexec
SELECT 'ALTER TABLE documentdb_data.documents_' || :'gate_availability_id' || ' DROP CONSTRAINT collection_pk_' || :'gate_availability_id' \gexec

SELECT 'ANALYZE documentdb_data.documents_' || :'planes_id' \gexec
SELECT 'ANALYZE documentdb_data.documents_' || :'gate_availability_id' \gexec

-- check indexes (ordered or not)
SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('lookupdb', '{ "listIndexes": "planes" }') ORDER BY 1;
SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('lookupdb', '{ "listIndexes": "gate_availability" }') ORDER BY 1;

BEGIN;
set local documentdb.forceBitmapScanForLookup to off;
set local documentdb.enableLookupInnerJoin to off;
SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
set local documentdb.enableLookupInnerJoin to on;
SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

set local documentdb.forceBitmapScanForLookup to on;
set local documentdb.enableLookupInnerJoin to off;
SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
set local documentdb.enableLookupInnerJoin to on;
SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
ROLLBACK;

-- Insert a lot more data
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..1000 LOOP
PERFORM documentdb_api.insert_one('lookupdb','planes',' { "model" : "A380", "price" : 280, "quantity" : 20 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','planes','{ "model" : "A340", "price" : 140, "quantity" : 1 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','planes',' { "model" : "A330", "price" : 10, "quantity" : 5 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','planes',' { "model" : "737", "price" : 50, "quantity" : 30 }', NULL);
END LOOP;
END;
$$;

DO $$
DECLARE i int;
BEGIN
FOR i IN 1..250 LOOP
PERFORM documentdb_api.insert_one('lookupdb','gate_availability',' { "plane_model" : "A330", "gates" : 30 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','gate_availability',' { "plane_model" : "A340", "gates" : 10 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','gate_availability','{ "plane_model" : "A380", "gates" : 5 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','gate_availability','{ "plane_model" : "A350", "gates" : 20 }', NULL);
PERFORM documentdb_api.insert_one('lookupdb','gate_availability','{ "plane_model" : "737", "gates" : 110 }', NULL);
END LOOP;
END;
$$;

-- Now test index usage
BEGIN;

set local documentdb.forceBitmapScanForLookup to on;
set local documentdb.enableLookupInnerJoin to off;
-- LEFT JOIN with force bitmap scan, should use materialize seq scan
EXPLAIN (SUMMARY OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

set local documentdb.enableLookupInnerJoin to on;
-- RIGHT JOIN with force bitmap scan, should use materialize seq scan
EXPLAIN (SUMMARY OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

set local documentdb.forceBitmapScanForLookup to off;
set local documentdb.enableLookupInnerJoin to off;

-- LEFT JOIN without force bitmap scan, should use index scan
EXPLAIN (SUMMARY OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

set local documentdb.enableLookupInnerJoin to on;

-- RIGHT JOIN without force bitmap scan, should use index scan
EXPLAIN (SUMMARY OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('lookupdb', 
    '{ "aggregate": "planes", "pipeline": [ { "$match": { "model": { "$exists": true } } }, { "$lookup": { "from": "gate_availability", "as": "matched_docs", "localField": "model", "foreignField": "plane_model" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

ROLLBACK;

-- Cleanup
SELECT documentdb_api.drop_collection('lookupdb', 'planes'), documentdb_api.drop_collection('lookupdb', 'gate_availability');

-- reset state
SELECT documentdb_api.drop_collection('lookupdb', 'Dishes'), documentdb_api.drop_collection('lookupdb', 'Ingredients'), documentdb_api.drop_collection('lookupdb', 'Shops');

-- lookup with point read where inner plan depends on external param crash fix scenario
SELECT documentdb_api.insert_one('lookupdb','Dishes',' { "_id" : 1, "dishId" : 1, "ingredients": [1, 2, 3, 4] }', NULL);
SELECT documentdb_api.insert_one('lookupdb','Ingredients',' { "_id" : 1, "shopId" : 101, "name": "Salt" }', NULL);
SELECT documentdb_api.insert_one('lookupdb','Ingredients',' { "_id" : 2, "shopId" : 101, "name": "Clove" }', NULL);
SELECT documentdb_api.insert_one('lookupdb','Ingredients',' { "_id" : 3, "shopId" : 101, "name": "Olive" }', NULL);
SELECT documentdb_api.insert_one('lookupdb','Ingredients',' { "_id" : 4, "shopId" : 101, "name": "Pepper" }', NULL);
SELECT documentdb_api.insert_one('lookupdb','Shops',' { "_id" : 101, "name": "ABC Mart" }', NULL);

-- Get collection IDs for each collection and store them in variables
SELECT collection_id AS dishes_id FROM documentdb_api_catalog.collections WHERE database_name = 'lookupdb' AND collection_name = 'Dishes' \gset
SELECT collection_id AS ingredients_id FROM documentdb_api_catalog.collections WHERE database_name = 'lookupdb' AND collection_name = 'Ingredients' \gset  
SELECT collection_id AS shops_id FROM documentdb_api_catalog.collections WHERE database_name = 'lookupdb' AND collection_name = 'Shops' \gset

-- Dynamically run ANALYZE commands using the collection IDs
SELECT 'ANALYZE documentdb_data.documents_' || :'dishes_id' \gexec
SELECT 'ANALYZE documentdb_data.documents_' || :'ingredients_id' \gexec
SELECT 'ANALYZE documentdb_data.documents_' || :'shops_id' \gexec

BEGIN;
set local seq_page_cost to 1;
set local documentdb.enableLookupInnerJoin to off;
SELECT document FROM bson_aggregation_pipeline('lookupdb', '{"aggregate": "Dishes", "pipeline": [{"$match": {"dishId": 1}}, {"$unwind": "$ingredients"} , {"$lookup": {"from": "Ingredients", "localField": "ingredients", "foreignField": "_id", "as": "ingredient_info"}} , {"$unwind": "$ingredient_info"} , {"$lookup": {"from": "Shops", "localField": "ingredient_info.shopId", "foreignField": "_id", "as": "shop_info"}}, {"$unwind": "$shop_info"}]}');
ROLLBACK;

BEGIN;
set local seq_page_cost to 1;
set local documentdb.enableLookupInnerJoin to off;
EXPLAIN (COSTS OFF, SUMMARY OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('lookupdb', '{"aggregate": "Dishes", "pipeline": [{"$match": {"dishId": 1}}, {"$unwind": "$ingredients"} , {"$lookup": {"from": "Ingredients", "localField": "ingredients", "foreignField": "_id", "as": "ingredient_info"}} , {"$unwind": "$ingredient_info"} , {"$lookup": {"from": "Shops", "localField": "ingredient_info.shopId", "foreignField": "_id", "as": "shop_info"}}, {"$unwind": "$shop_info"}]}');
ROLLBACK;