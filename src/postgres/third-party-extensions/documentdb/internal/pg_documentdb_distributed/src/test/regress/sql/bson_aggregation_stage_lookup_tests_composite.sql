SET citus.next_shard_id TO 951000;
SET documentdb.next_collection_id TO 9510;
SET documentdb.next_collection_index_id TO 9510;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

set documentdb.defaultUseCompositeOpClass to on;

-- Insert data
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 1, "item_name" : "shirt", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases', '{ "_id" : 2, "item_name" : "pants", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 3, "item_name" : "hat", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 4, "item_name" : ["shirt", "hat", "pants"], "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 6, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases',' { "_id" : 7, "item_name" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('compdb','catalog_items',' { "_id" : 1, "item_code" : "shirt", "product_description": "product 1", "stock_quantity" : 120 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items',' { "_id" : 11, "item_code" : "shirt", "product_description": "product 1", "stock_quantity" : 240 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 2, "item_code" : "hat", "product_description": "product 2", "stock_quantity" : 80 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 3, "item_code" : "shoes", "product_description": "product 3", "stock_quantity" : 60 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 4, "item_code" : "pants", "product_description": "product 4", "stock_quantity" : 70 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 5, "item_code" : null, "product_description": "product 4", "stock_quantity" : 70 }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 6, "item_code" :  {"a": "x", "b" : 1, "c" : [1, 2, 3]}, "product_description": "complex object" }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 7, "item_code" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "product_description": "complex array" }', NULL);
SELECT documentdb_api.insert_one('compdb','catalog_items','{ "_id" : 8, "item_code" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "product_description": "complex array" }', NULL);



-- Create Index on catalog_items
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'compdb',
  '{
     "createIndexes": "catalog_items",
     "indexes": [
       {"key": {"item_code": 1}, "name": "idx_catalog_items_item_code"}
     ]
   }',
   true
);

-- Test Index pushdown
SELECT documentdb_distributed_test_helpers.drop_primary_key('compdb','catalog_items');

BEGIN;
set local enable_seqscan TO off;
EXPLAIN(costs off) SELECT document FROM bson_aggregation_pipeline('compdb', '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs" } } ]}');
ROLLBACK;

SELECT document FROM bson_aggregation_pipeline('compdb', '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs" } } ]}');

-- Insert data into a new collection to be sharded (shard key can't be an array, but can be null or object)
SELECT documentdb_api.insert_one('compdb','customer_purchases_sharded',' { "_id" : 1, "item_name" : "shirt", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases_sharded','{ "_id" : 2, "item_name" : "pants", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases_sharded',' { "_id" : 3, "item_name" : "hat", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases_sharded',' { "_id" : 4}', NULL);
SELECT documentdb_api.insert_one('compdb','customer_purchases_sharded',' { "_id" : 5, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);

-- Shard customer_purchases collection on item_name 
SELECT documentdb_api.shard_collection('compdb','customer_purchases_sharded', '{"item_name":"hashed"}', false);

-- Test Index pushdown on sharded left collection
BEGIN;
set local enable_seqscan TO off;
SET JIT To off;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "customer_purchases_sharded", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs" } } ]}')
$Q$);
ROLLBACK;

SELECT shard_key_value, object_id, document FROM documentdb_api.collection('compdb', 'customer_purchases_sharded') order by object_id;


-- Test lookup on sharded left collection
SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "customer_purchases_sharded", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs" } } ]}');

SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "customer_purchases_sharded", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs" } }, { "$addFields": { "matched_docs.hello": "newsubpipelinefield" } } ]}');

SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "customer_purchases_sharded", "pipeline": [ { "$lookup": { "from": "catalog_items", "localField": "item_name", "foreignField": "item_code", "as": "matched_docs", "pipeline": [ { "$addFields": { "matched_docs.hello": "newsubpipelinefield" } } ] } } ]}');


-- Test coalesce path, return empty array when no match found 
SELECT documentdb_api.insert_one('compdb','coalesce_source','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('compdb','coalesce_source','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('compdb','coalesce_source','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('compdb','coalesce_foreign','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('compdb','coalesce_foreign','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('compdb','coalesce_foreign','{"_id": 2}', NULL);

SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "coalesce_source", "pipeline": [ { "$lookup": { "from": "coalesce_foreign", "localField": "a", "foreignField": "nonex", "as": "matched_docs" } } ]}');

-- Test dotted path
SELECT documentdb_api.insert_one('compdb','dotted_path_source','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_source','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_source','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_source','{"_id": 3, "a": {"c": 1}}', NULL);

SELECT documentdb_api.insert_one('compdb','dotted_path_foreign','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_foreign','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_foreign','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_foreign','{"_id": 3, "b": {"c" : 1}}', NULL);
SELECT documentdb_api.insert_one('compdb','dotted_path_foreign','{"_id": 4, "b": {"c" : 2}}', NULL);

SELECT document FROM bson_aggregation_pipeline('compdb',
    '{ "aggregate": "dotted_path_source", "pipeline": [ { "$lookup": { "from": "dotted_path_foreign", "localField": "a.c", "foreignField": "b.c", "as": "matched_docs" } } ]}');


-- (B) Lookup with pipeline support

-- (B).1 Data Ingestion
SELECT documentdb_api.insert_one('compdb','establishments',' {"_id": 1, "establishment_name": "The Grand Diner", "dishes": ["burger", "fries"], "order_quantity": 100 , "drinks": ["soda", "juice"]}', NULL);
SELECT documentdb_api.insert_one('compdb','establishments','{ "_id": 2, "establishment_name": "Pizza Palace", "dishes": ["veggie pizza", "meat pizza"], "order_quantity": 120, "drinks": ["soda"]}', NULL);

SELECT documentdb_api.insert_one('compdb','menu','{ "_id": 1, "item_name": "burger", "establishment_name": "The Grand Diner"}', NULL);
SELECT documentdb_api.insert_one('compdb','menu','{ "_id": 2, "item_name": "veggie pizza", "establishment_name": "Pizza Palace", "drink": "water"}', NULL);
SELECT documentdb_api.insert_one('compdb','menu','{ "_id": 3, "item_name": "veggie pizza", "establishment_name": "Pizza Palace", "drink": "soda"}', NULL);

-- (B).1 Simple Lookup with pipeline

-- {
-- 	$lookup :
-- 	{
-- 		from : establishments,
--		to:	menu,
--		localField: establishment_name,
--		foreignField: establishment_name,
--		pipeline: {
--			[
--				{
--					$match :  { order_quantity : { $gt, 110}}
--				}
--			]
--		}
--	}
--}

SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');


-- (B).2 Index creation on the from collection
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'compdb',
  '{
     "createIndexes": "establishments",
     "indexes": [
       {"key": {"establishment_name": 1}, "name": "idx_establishments_establishment_name"},
	   {"key": {"order_quantity": 1}, "name": "idx_establishments_order_quantity"},
       {"key": {"establishment_name": 1, "order_quantity": 1}, "name": "idx_establishments_establishment_name_order_quantity"}
     ]
   }',
   true
);

SELECT documentdb_api_catalog.bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('compdb', '{ "listIndexes": "establishments" }') ORDER BY 1;

-- (B).3.a Index usage
BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- Adding a $sort in the pipeline
BEGIN;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- Adding a $sort that can be pushed down in the pipeline
BEGIN;
SET LOCAL enable_seqscan TO off;
set local documentdb.enableIndexOrderbyPushdown to on;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}, { "$sort": { "establishment_name": 1 }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- (B).3.b Index usage with optimization: (if lookup has a join condition and the lookup pipeline has $match as the first 
-- stage we push the $match filter up with the join. If both conditions are one same property both the filters should be 
-- part of the index condition)

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "establishment_name" : { "$eq" : "Pizza Palace" } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "establishment_name" : { "$eq" : "Pizza Palace" } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- (B).4 Nested lookup pipeline (lookup pipeline containing lookup)

-- {
-- 	$lookup :
-- 	{
-- 		from : establishments,
--		to:	menu,
--		localField: establishment_name,
--		foreignField: establishment_name,
--		pipeline: {
--			[
--				{
--					$lookup :  {
-- 						from : establishments,
--						to:	establishments,
--						localField: _id,
--						foreignField: _id,
--						pipeline: {
--							[
--								{ unwind : "dishes"}
--							]
--						}
--					}
--				}
--			]
--		}
--	}
--}
SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$dishes" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');

-- (B).5 Nested lookup pipeline (lookup pipeline containing lookup) index usage

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$dishes" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "bar": "$dishes" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;



BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$sort": { "dishes": 1 } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('compdb', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "_id": "bar" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;


-- Lookup Tests for array index-based paths

SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 1, "a" : [{"x" : 1}] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 2, "a" : {"x" : 1} }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 3, "a" : [{"x": 2}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 4, "a" : [{"y": 1}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 5, "a" : [2, 3] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 6, "a" : {"x": [1, 2]} }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 7, "a" : [{"x": 1}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 8, "a" : [{"x": [1, 2]}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('compdb','array_path_purchases',' { "_id" : 9, "a" : [[{"x": [1, 2]}, {"y": 1}]] }', NULL);

SELECT documentdb_api.insert_one('compdb','array_path_items',' { "_id" : 1, "b" : 1 }', NULL);

SELECT document FROM bson_aggregation_pipeline('compdb', '{ "aggregate": "array_path_purchases", "pipeline": [ { "$lookup": { "from": "array_path_items", "localField": "a.0.x", "foreignField": "b", "as": "matched_docs" }} ]}');

SELECT document FROM bson_aggregation_pipeline('compdb', '{ "aggregate": "array_path_items", "pipeline": [ { "$lookup": { "from": "array_path_purchases", "localField": "b", "foreignField": "a.0.x", "as": "matched_docs" }} ]}');

-- Test for Index push down crash when Foreign Field = '_id' and the target collection is sharded 
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 1, "item_name" : "itemA", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test','{ "_id" : 2, "item_name" : "itemE", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 3, "item_name" : "itemC", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 4, "item_name" : ["itemA", "itemC", "itemE"], "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 6, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('compdb','purchases_pushdown_test',' { "_id" : 7, "item_name" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('compdb','items_pushdown_test',' { "_id" :  "itemA", "item_description": "product 1", "in_stock" : 120 }', NULL);
SELECT documentdb_api.insert_one('compdb','items_pushdown_test',' { "_id" :  "itemB", "item_description": "product 1", "in_stock" : 240 }', NULL);
SELECT documentdb_api.insert_one('compdb','items_pushdown_test','{ "_id" :  "itemC", "item_description": "product 2", "in_stock" : 80 }', NULL);
SELECT documentdb_api.insert_one('compdb','items_pushdown_test','{ "_id" :  "itemD", "item_description": "product 3", "in_stock" : 60 }', NULL);
SELECT documentdb_api.insert_one('compdb','items_pushdown_test','{ "_id" :  "itemE", "item_description": "product 4", "in_stock" : 70 }', NULL);
SELECT documentdb_api.insert_one('compdb','items_pushdown_test','{ "_id" :  "itemF", "item_description": "product 4", "in_stock" : 70 }', NULL);

SELECT documentdb_api.shard_collection('compdb','items_pushdown_test', '{"_id":"hashed"}', false);


SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "purchases_pushdown_test", "pipeline": [ { "$lookup": { "from": "items_pushdown_test", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

-- Test for removal of inner join
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "customer_purchases", "indexes": [ { "key": { "item_name": 1 }, "name": "customer_purchases_item_name_1" } ] }', TRUE);

BEGIN;
set local enable_seqscan to off;
SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$match": { "item_name": { "$exists": true } } }, { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "item_code" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
ROLLBACK;

BEGIN;
set local enable_seqscan to off;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('compdb', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$match": { "item_name": { "$exists": true } } }, { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "item_code" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
ROLLBACK;

-- do a multi layer lookup that matches on _id
SELECT COUNT(documentdb_api.insert_one('compdb', 'source1', FORMAT('{ "_id": %s, "field1": "value%s", "fieldsource1": "foo" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('compdb', 'source2', FORMAT('{ "_id": "value%s", "field2": "othervalue%s", "fieldsource2": "foobar" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('compdb', 'source3', FORMAT('{ "_id": "othervalue%s", "field3": "someothervalue%s", "fieldsource3": "foobarfoo" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('compdb', 'source4', FORMAT('{ "_id": "someothervalue%s", "field4": "yetsomeothervalue%s", "fieldsource4": "foobarbaz" }', i, i)::bson)) FROM generate_series(1, 100) i;

-- create indexes on the intermediate fields
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "source1", "indexes": [ { "key": { "field1": 1 }, "name": "field1_1" } ] }', TRUE);

ANALYZE documentdb_data.documents_9520;
ANALYZE documentdb_data.documents_9521;
ANALYZE documentdb_data.documents_9522;
ANALYZE documentdb_data.documents_9523;

-- should always pick up _id index.
BEGIN;
set local enable_indexscan to off;
set local enable_bitmapscan to off;
set local enable_material to off;
EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('compdb',
  '{ "aggregate": "source1", "pipeline": [ 
    { "$match": { "field1": { "$eq": "value10" } } },
    { "$lookup": { "from": "source2", "as": "source2_docs", "localField": "field1", "foreignField": "_id" } },
    { "$unwind": { "path": "$source2_docs", "preserveNullAndEmptyArrays": true } },
    { "$lookup": { "from": "source3", "as": "source3_docs", "localField": "source2_docs", "foreignField": "_id" } },
    { "$unwind": { "path": "$source3_docs", "preserveNullAndEmptyArrays": true } },
    { "$lookup": { "from": "source4", "as": "source4_docs", "localField": "source3_docs", "foreignField": "_id" } },
    { "$unwind": { "path": "$source4_docs", "preserveNullAndEmptyArrays": true } }
  ]}');
ROLLBACK;
