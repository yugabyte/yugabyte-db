SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

-- Insert data
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 1, "item_name" : "shirt", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases','{ "_id" : 2, "item_name" : "pants", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 3, "item_name" : "hat", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 4, "item_name" : ["shirt", "hat", "pants"], "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 6, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases',' { "_id" : 7, "item_name" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('db','catalog_items',' { "_id" : 1, "item_code" : "shirt", "product_description": "product 1", "stock_quantity" : 120 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items',' { "_id" : 11, "item_code" : "shirt", "product_description": "product 1", "stock_quantity" : 240 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 2, "item_code" : "hat", "product_description": "product 2", "stock_quantity" : 80 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 3, "item_code" : "shoes", "product_description": "product 3", "stock_quantity" : 60 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 4, "item_code" : "pants", "product_description": "product 4", "stock_quantity" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 5, "item_code" : null, "product_description": "product 4", "stock_quantity" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 6, "item_code" :  {"a": "x", "b" : 1, "c" : [1, 2, 3]}, "product_description": "complex object" }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 7, "item_code" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "product_description": "complex array" }', NULL);
SELECT documentdb_api.insert_one('db','catalog_items','{ "_id" : 8, "item_code" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "product_description": "complex array" }', NULL);



-- Test filter generation 
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') FROM documentdb_api.collection('db', 'customer_purchases');

-- Test full lookup sql
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match FROM documentdb_api.collection('db', 'customer_purchases')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Create Index on catalog_items
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "catalog_items",
     "indexes": [
       {"key": {"item_code": 1}, "name": "idx_catalog_items_item_code"}
     ]
   }',
   true
);

-- Test Index pushdown
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','catalog_items');
BEGIN;
set local enable_seqscan TO off;
EXPLAIN(costs off) WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
FROM documentdb_api.collection('db', 'catalog_items') AS t2 
WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
ROLLBACK;

-- Insert data into a new collection to be sharded (shard key can't be an array, but can be null or object)
SELECT documentdb_api.insert_one('db','customer_purchases_sharded',' { "_id" : 1, "item_name" : "shirt", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases_sharded','{ "_id" : 2, "item_name" : "pants", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases_sharded',' { "_id" : 3, "item_name" : "hat", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases_sharded',' { "_id" : 4}', NULL);
SELECT documentdb_api.insert_one('db','customer_purchases_sharded',' { "_id" : 5, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);

-- Shard customer_purchases collection on item_name 
SELECT documentdb_api.shard_collection('db','customer_purchases_sharded', '{"item_name":"hashed"}', false);

-- Test filter generation on sharded left collection
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') FROM documentdb_api.collection('db', 'customer_purchases_sharded');

-- Test Index pushdown on sharded left collection
BEGIN;
set local enable_seqscan TO off;
SET JIT To off;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) 
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
$Q$);
ROLLBACK;

SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'customer_purchases_sharded') order by object_id;


-- Test lookup on sharded left collection
WITH 
t1_0 AS(SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
  AS lookup_filters FROM documentdb_api.collection('db', 'customer_purchases_sharded')) 
SELECT bson_dollar_lookup_project(t1_0.document, t2_0_agg.matched_array , 'matched_docs'::text) as document 
FROM t1_0 LEFT JOIN LATERAL ( 
    WITH "LookupStage_0_0" as (SELECT * FROM documentdb_api.collection('db', 'catalog_items') AS t2_0 WHERE bson_dollar_in(t2_0.document, t1_0.lookup_filters) ) 
    SELECT COALESCE(array_agg("LookupStage_0_0".document::documentdb_core.bson), '{}'::bson[]) as matched_array FROM "LookupStage_0_0"
) t2_0_agg ON TRUE;

-- Test lookup on sharded left collection (rewrite avoiding the the CTE error)
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
 
-- Apply subpipeline on the out side of the join. This is a better execution plan since 
-- we are not applying the subpipeline on the entire 'from' collection
WITH simple_lookup as
(
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded'))
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
-- perform lookup
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE
) 
-- subpipeline moved down 
SELECT bson_dollar_add_fields(bson_dollar_lookup_project, '{"matched_docs.hello": "newsubpipelinefield"}') from simple_lookup;


WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded')),
-- subpipeline moved up
t2 as (SELECT bson_dollar_add_fields(document, '{"matched_docs.hello": "newsubpipelinefield"}') as document from documentdb_api.collection('db', 'catalog_items'))
-- now perform lookup
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Lookup subpipeline applied after the join is done (this is the community version semantics)
WITH lookup_stage as
(
  -- Create CTE for the left/source collection (say t1)
  WITH                                                                                            
    t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	  AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded'))
  SELECT t1.document, t2_agg.document as matched
  FROM t1
  -- perform lateral join on the right collection (aka joined collection)
    LEFT JOIN LATERAL ( 
	  SELECT document
	  FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	  WHERE bson_dollar_in(t2.document, t1.match) 
  ) t2_agg ON TRUE
),
-- Apply subpipeline after lateral join 
subpipeline_stage1 as ( SELECT lookup_stage.document, bson_dollar_add_fields(lookup_stage.matched, '{"new": "field"}') as matched from lookup_stage)
-- Aggregate the results so, if there multiple matches they are folded in an array
SELECT bson_dollar_lookup_project(subpipeline_stage1.document, 
                COALESCE(array_agg(subpipeline_stage1.matched::documentdb_core.bson), '{}'::bson[]), 'matched_docs'::text) FROM subpipeline_stage1
GROUP BY subpipeline_stage1.document;

set citus.enable_local_execution to off;

-- Test lookup on sharded left collection
WITH 
t1_0 AS(SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
  AS lookup_filters FROM documentdb_api.collection('db', 'customer_purchases_sharded')) 
SELECT bson_dollar_lookup_project(t1_0.document, t2_0_agg.matched_array , 'matched_docs'::text) as document 
FROM t1_0 LEFT JOIN LATERAL ( 
    WITH "LookupStage_0_0" as (SELECT * FROM documentdb_api.collection('db', 'catalog_items') AS t2_0 WHERE bson_dollar_in(t2_0.document, t1_0.lookup_filters) ) 
    SELECT COALESCE(array_agg("LookupStage_0_0".document::documentdb_core.bson), '{}'::bson[]) as matched_array FROM "LookupStage_0_0"
) t2_0_agg ON TRUE;

-- Test lookup on sharded left collection (rewrite avoiding the the CTE error)
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
 
-- Apply subpipeline on the out side of the join. This is a better execution plan since 
-- we are not applying the subpipeline on the entire 'from' collection
WITH simple_lookup as
(
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded'))
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
-- perform lookup
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE
) 
-- subpipeline moved down 
SELECT bson_dollar_add_fields(bson_dollar_lookup_project, '{"matched_docs.hello": "newsubpipelinefield"}') from simple_lookup;


WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded')),
-- subpipeline moved up
t2 as (SELECT bson_dollar_add_fields(document, '{"matched_docs.hello": "newsubpipelinefield"}') as document from documentdb_api.collection('db', 'catalog_items'))
-- now perform lookup
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Lookup subpipeline applied after the join is done (this is the community version semantics)
WITH lookup_stage as
(
  -- Create CTE for the left/source collection (say t1)
  WITH                                                                                            
    t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"item_code" : "item_name"}') 
	  AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'customer_purchases_sharded'))
  SELECT t1.document, t2_agg.document as matched
  FROM t1
  -- perform lateral join on the right collection (aka joined collection)
    LEFT JOIN LATERAL ( 
	  SELECT document
	  FROM documentdb_api.collection('db', 'catalog_items') AS t2 
	  WHERE bson_dollar_in(t2.document, t1.match) 
  ) t2_agg ON TRUE
),
-- Apply subpipeline after lateral join 
subpipeline_stage1 as ( SELECT lookup_stage.document, bson_dollar_add_fields(lookup_stage.matched, '{"new": "field"}') as matched from lookup_stage)
-- Aggregate the results so, if there multiple matches they are folded in an array
SELECT bson_dollar_lookup_project(subpipeline_stage1.document, 
                COALESCE(array_agg(subpipeline_stage1.matched::documentdb_core.bson), '{}'::bson[]), 'matched_docs'::text) FROM subpipeline_stage1
GROUP BY subpipeline_stage1.document;

set citus.enable_local_execution to on;

-- Test coalesce path, return empty array when no match found 
SELECT documentdb_api.insert_one('db','coalesce_source','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('db','coalesce_source','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('db','coalesce_source','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','coalesce_foreign','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('db','coalesce_foreign','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('db','coalesce_foreign','{"_id": 2}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"nonex" : "a"}') FROM documentdb_api.collection('db', 'coalesce_source');
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"nonex" : "a"}') 
	AS match FROM documentdb_api.collection('db', 'coalesce_source')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'coalesce_foreign') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Test dotted path

SELECT documentdb_api.insert_one('db','dotted_path_source','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_source','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_source','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_source','{"_id": 3, "a": {"c": 1}}', NULL);

SELECT documentdb_api.insert_one('db','dotted_path_foreign','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_foreign','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_foreign','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_foreign','{"_id": 3, "b": {"c" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','dotted_path_foreign','{"_id": 4, "b": {"c" : 2}}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') FROM documentdb_api.collection('db', 'dotted_path_source');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') 
	AS match FROM documentdb_api.collection('db', 'dotted_path_source')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'dotted_path_foreign') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Empty from collection tests

SELECT * FROM documentdb_api.collection('db', 'from1NonExistent');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') 
	AS match FROM documentdb_api.collection('db', 'dotted_path_source')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'from1NonExistent') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Dotted path on mathched array name (some.documents)

SELECT documentdb_api.insert_one('db','dotted_as_source2','{"_id": 0, "a": {"b" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','dotted_as_source2','{"_id": 1}', NULL);
SELECT documentdb_api.insert_one('db','dotted_as_foreign2','{"_id": 0, "target": 1}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"target" : "a.b"}') FROM documentdb_api.collection('db', 'dotted_as_source2');
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"target" : "a.b"}') 
	AS match FROM documentdb_api.collection('db', 'dotted_as_source2')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'same.documents'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'dotted_as_foreign2') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Dotted path that goes through objects and arrays 

SELECT documentdb_api.insert_one('db','dotted_source','{"_id": 0, "a": [{"b": 1}, {"b": 2}]}', NULL);
SELECT documentdb_api.insert_one('db','dotted_foreign','{"_id": 0}', NULL);
SELECT documentdb_api.insert_one('db','dotted_foreign','{"_id": 1}', NULL);
SELECT documentdb_api.insert_one('db','dotted_foreign','{"_id": 2}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"_id" : "a.b"}') FROM documentdb_api.collection('db', 'dotted_source');

-- Dotted AS path that is already present in the left document 
SELECT documentdb_api.insert_one('db','dotted_as_source','{"_id": 0, "a": [{"b": 1}, {"b": 2}]}', NULL);
SELECT documentdb_api.insert_one('db','dotted_as_source','{"_id": 1, "c": [{"d": 1}, {"d": 2}]}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"c.d" : "a.b"}') FROM documentdb_api.collection('db', 'dotted_as_source');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"c.d" : "a.b"}') 
	AS match FROM documentdb_api.collection('db', 'dotted_as_source')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'a.b'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'dotted_as_source') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- (B) Lookup with pipeline support

-- (B).1 Data Ingestion
SELECT documentdb_api.insert_one('db','establishments',' {"_id": 1, "establishment_name": "The Grand Diner", "dishes": ["burger", "fries"], "order_quantity": 100 , "drinks": ["soda", "juice"]}', NULL);
SELECT documentdb_api.insert_one('db','establishments','{ "_id": 2, "establishment_name": "Pizza Palace", "dishes": ["veggie pizza", "meat pizza"], "order_quantity": 120, "drinks": ["soda"]}', NULL);

SELECT documentdb_api.insert_one('db','menu','{ "_id": 1, "item_name": "burger", "establishment_name": "The Grand Diner"}', NULL);
SELECT documentdb_api.insert_one('db','menu','{ "_id": 2, "item_name": "veggie pizza", "establishment_name": "Pizza Palace", "drink": "water"}', NULL);
SELECT documentdb_api.insert_one('db','menu','{ "_id": 3, "item_name": "veggie pizza", "establishment_name": "Pizza Palace", "drink": "soda"}', NULL);

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

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');


-- (B).2 Index creation on the from collection

  SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "establishments",
     "indexes": [
       {"key": {"establishment_name": 1}, "name": "idx_establishments_establishment_name"},
	     {"key": {"order_quantity": 1}, "name": "idx_establishments_order_quantity"}
     ]
   }',
   true
);

-- (B).3.a Index usage
BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- Adding a $sort in the pipeline
BEGIN;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "order_quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

-- (B).3.b Index usage with optimization: (if lookup has a join condition and the lookup pipeline has $match as the first 
-- stage we push the $match filter up with the join. If both conditions are one same property both the filters should be 
-- part of the index condition)

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$match": { "establishment_name" : { "$eq" : "Pizza Palace" } }}], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
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
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$dishes" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');

-- (B).5 Nested lookup pipeline (lookup pipeline containing lookup) index usage

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$dishes" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "bar": "$dishes" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;



BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$sort": { "dishes": 1 } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "menu", "pipeline": [ { "$lookup": { "from": "establishments", "pipeline": [ { "$lookup": { "from": "establishments", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "_id": "bar" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "establishment_name", "foreignField": "establishment_name" }} ], "cursor": {} }');
ROLLBACK;


-- Lookup Tests for array index-based paths

SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 1, "a" : [{"x" : 1}] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 2, "a" : {"x" : 1} }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 3, "a" : [{"x": 2}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 4, "a" : [{"y": 1}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 5, "a" : [2, 3] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 6, "a" : {"x": [1, 2]} }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 7, "a" : [{"x": 1}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 8, "a" : [{"x": [1, 2]}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','array_path_purchases',' { "_id" : 9, "a" : [[{"x": [1, 2]}, {"y": 1}]] }', NULL);

SELECT documentdb_api.insert_one('db','array_path_items',' { "_id" : 1, "b" : 1 }', NULL);

-- Test filter generation 
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.x"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.x"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.x.0"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.1.y"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.0.x"}'), document FROM documentdb_api.collection('db', 'array_path_purchases');
-- Test full lookup sql
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.x"}') 
	AS match FROM documentdb_api.collection('db', 'array_path_purchases')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'array_path_items') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;


WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"a.0.x" : "b"}') 
	AS match FROM documentdb_api.collection('db', 'array_path_items')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'array_path_purchases') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;


-- test lookup with operator type document contents
SELECT documentdb_api.insert_one('db', 'operator_lookup_left', '{ "_id": 1, "a": 1, "b": { "$isArray": 1, "OtherField": 1 }}');
SELECT documentdb_api.insert_one('db', 'operator_lookup_right', '{ "_id": 1, "a": 1, "b": { "$isArray": 1, "OtherField": 1 }}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "operator_lookup_left", "pipeline": [ { "$lookup": { "from": "operator_lookup_right", "localField": "a", "foreignField": "a", "as": "myfoo" } }] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "operator_lookup_left", "pipeline": [ { "$lookup": { "from": "operator_lookup_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "operator_lookup_left", "pipeline": [ { "$lookup": { "from": "operator_lookup_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "operator_lookup_left", "pipeline": [ { "$project": { "_id": "500" }}, { "$lookup": { "from": "operator_lookup_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');

-- Test for Index push down crash when Foreign Field = '_id' and the target collection is sharded 
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 1, "item_name" : "itemA", "price" : 12, "order_quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test','{ "_id" : 2, "item_name" : "itemE", "price" : 20, "order_quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 3, "item_name" : "itemC", "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 4, "item_name" : ["itemA", "itemC", "itemE"], "price" : 10, "order_quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 6, "item_name" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('db','purchases_pushdown_test',' { "_id" : 7, "item_name" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('db','items_pushdown_test',' { "_id" :  "itemA", "item_description": "product 1", "in_stock" : 120 }', NULL);
SELECT documentdb_api.insert_one('db','items_pushdown_test',' { "_id" :  "itemB", "item_description": "product 1", "in_stock" : 240 }', NULL);
SELECT documentdb_api.insert_one('db','items_pushdown_test','{ "_id" :  "itemC", "item_description": "product 2", "in_stock" : 80 }', NULL);
SELECT documentdb_api.insert_one('db','items_pushdown_test','{ "_id" :  "itemD", "item_description": "product 3", "in_stock" : 60 }', NULL);
SELECT documentdb_api.insert_one('db','items_pushdown_test','{ "_id" :  "itemE", "item_description": "product 4", "in_stock" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','items_pushdown_test','{ "_id" :  "itemF", "item_description": "product 4", "in_stock" : 70 }', NULL);

SELECT documentdb_api.shard_collection('db','items_pushdown_test', '{"_id":"hashed"}', false);


SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "purchases_pushdown_test", "pipeline": [ { "$lookup": { "from": "items_pushdown_test", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN



-- UDF Unit test for merge documents at path
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path(NULL, NULL, NULL);
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": "text"}', '{}', 'a');
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": "text"}', '{ "b" : true}', 'a');
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": "text"}', '{ "b" : true}', 'd');
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": { "b": "text", "c": true }}', '{ "random" : false }', 'a');
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": { "b": "text", "c": true }}', '{ "random" : false }', 'a.b');
SELECT documentdb_api_internal.bson_dollar_merge_documents_at_path('{"a": [{ "b": "text", "c": true }, { "b": "text2", "c": false }]}', '{ "random" : false }', 'a.b');

-- UDF tests for merge documents with override Array
SELECT documentdb_api_internal.bson_dollar_merge_documents(NULL, NULL, NULL);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": "text"}', '{}', false);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": "text"}', '{ "a" : true}', false);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": { "b": "text", "c": true }}', '{ "a.b.random" : false }', false);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": [{ "b": "text", "c": true }, { "b": "text2", "c": false }]}', '{ "a.b.random" : false }', false);

SELECT documentdb_api_internal.bson_dollar_merge_documents(NULL, NULL, NULL);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": "text"}', '{}', true);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": "text"}', '{ "a" : true}', true);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": { "b": "text", "c": true }}', '{ "a.b.random" : false }', true);
SELECT documentdb_api_internal.bson_dollar_merge_documents('{"a": [{ "b": "text", "c": true }, { "b": "text2", "c": false }]}', '{ "a.b.random" : false }', true);

-- todo: remove these duplicated tests
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

-- Test for removal of inner join
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "customer_purchases", "indexes": [ { "key": { "item_name": 1 }, "name": "customer_purchases_item_name_1" } ] }', TRUE);

BEGIN;
set local enable_seqscan to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$match": { "item_name": { "$exists": true } } }, { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "item_code" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
ROLLBACK;

BEGIN;
set local enable_seqscan to off;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "customer_purchases", "pipeline": [ { "$match": { "item_name": { "$exists": true } } }, { "$lookup": { "from": "catalog_items", "as": "matched_docs", "localField": "item_name", "foreignField": "item_code" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
ROLLBACK;

-- do a multi layer lookup that matches on _id
SELECT COUNT(documentdb_api.insert_one('db', 'source1', FORMAT('{ "_id": %s, "field1": "value%s", "fieldsource1": "foo" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('db', 'source2', FORMAT('{ "_id": "value%s", "field2": "othervalue%s", "fieldsource2": "foobar" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('db', 'source3', FORMAT('{ "_id": "othervalue%s", "field3": "someothervalue%s", "fieldsource3": "foobarfoo" }', i, i)::bson)) FROM generate_series(1, 100) i;
SELECT COUNT(documentdb_api.insert_one('db', 'source4', FORMAT('{ "_id": "someothervalue%s", "field4": "yetsomeothervalue%s", "fieldsource4": "foobarbaz" }', i, i)::bson)) FROM generate_series(1, 100) i;

-- create indexes on the intermediate fields
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "source1", "indexes": [ { "key": { "field1": 1 }, "name": "field1_1" } ] }', TRUE);

ANALYZE documentdb_data.documents_6020;
ANALYZE documentdb_data.documents_6021;
ANALYZE documentdb_data.documents_6022;
ANALYZE documentdb_data.documents_6023;

-- should always pick up _id index.
BEGIN;
set local enable_indexscan to off;
set local enable_bitmapscan to off;
set local enable_material to off;
EXPLAIN (COSTS OFF, ANALYZE ON, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db',
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

-- Test lookup query building works with aggregates
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "abc", "pipeline": [
      { "$lookup": 
       { 
        "from": "def", 
        "as": "lookup_count", 
        "let" : { "a" : "$A", "b" : "$B", "c" : "$C" }, 
        "pipeline": [
         { 
          "$group": { 
           "_id": "$groupId",
           "total_score": { "$sum": "$Abc.score" }, 
           "count": { "$sum": 1 } 
          } 
        }] 
      }}]}');


SELECT documentdb_api.create_collection('db', 'tenantCollection');
SELECT documentdb_api.create_collection('db', 'candidateRequest');
SELECT documentdb_api.insert('db','{ "insert" : "tenantCollection",  
"documents" : [
{"_id": 1, "name": "Tenant 1", "archived": false },
{"_id": 2, "name": "Tenant 2", "archived": false },
{"_id": 3, "name": "Tenant 3", "archived": false },
{"_id": 4, "name": "Tenant 4", "archived": false },
{"_id": 5, "name": "Tenant 5", "archived": false },
{"_id": 6, "name": "Tenant 6", "archived": false },
{"_id": 7, "name": "Tenant 7", "archived": false },
{"_id": 8, "name": "Tenant 8", "archived": false },
{"_id": 9, "name": "Tenant 9", "archived": false },
{"_id": 10, "name": "Tenant 10", "archived": false }
]}');

SELECT documentdb_api.insert('db','{ "insert" : "candidateRequest",  
"documents" : [
{ "_id": 101, "tenantId": 1, "candidateId": 201, "status": "closed", "createdAt": "2025-08-14T21:50:54.099Z" },
{ "_id": 102, "tenantId": 2, "candidateId": 202, "status": "open", "createdAt": "2025-08-15T10:15:22.399Z" },
{ "_id": 103, "tenantId": 3, "candidateId": 203, "status": "closed", "createdAt": "2025-08-16T08:20:14.221Z" },
{ "_id": 104, "tenantId": 4, "candidateId": 204, "status": "closed", "createdAt": "2025-08-17T12:47:11.912Z" },
{ "_id": 105, "tenantId": 5, "candidateId": 205, "status": "open", "createdAt": "2025-08-18T09:32:44.511Z" },
{ "_id": 106, "tenantId": 6, "candidateId": 206, "status": "closed", "createdAt": "2025-08-19T14:05:31.742Z" },
{ "_id": 107, "tenantId": 7, "candidateId": 207, "status": "closed", "createdAt": "2025-08-20T11:18:58.287Z" },
{ "_id": 108, "tenantId": 8, "candidateId": 208, "status": "open", "createdAt": "2025-08-21T07:44:21.663Z" },
{ "_id": 109, "tenantId": 9, "candidateId": 209, "status": "closed", "createdAt": "2025-08-22T16:13:49.984Z" },
{ "_id": 110, "tenantId": 10, "candidateId": 210, "status": "closed", "createdAt": "2025-08-23T19:28:33.527Z" }
]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "candidateRequest",
     "indexes": [
       {"key": {"tenantId": 1}, "name": "idx_candidateRequest_tenantId"}
     ]
   }',
   true
);

-- This Transaction shows buggy output we will remove this GUC soon and will remove this test as well
BEGIN;
SET LOCAL documentdb.enableUseLookupNewProjectInlineMethod TO off;
SELECT document FROM bson_aggregation_pipeline(
    'db',
    '{
      "aggregate": "tenantCollection",
      "pipeline": [
        {
          "$lookup": {
            "from": "candidateRequest",
            "localField": "_id",
            "foreignField": "tenantId",
            "as": "requests",
            "pipeline": [
              { "$match": { "status": { "$ne": "unopened" } } },
              { "$project": { "status": 1, "candidateId": 1 } }
            ]
          }
        }
      ]
    }'
);

ROLLBACK;

BEGIN;
SET LOCAL  documentdb.enableUseLookupNewProjectInlineMethod TO on;
SET LOCAL enable_seqscan TO off;
SELECT document FROM bson_aggregation_pipeline(
    'db',
    '{
      "aggregate": "tenantCollection",
      "pipeline": [
        {
          "$lookup": {
            "from": "candidateRequest",
            "localField": "_id",
            "foreignField": "tenantId",
            "as": "requests",
            "pipeline": [
              { "$match": { "status": { "$ne": "unopened" } } },
              { "$project": { "status": 1, "candidateId": 1 } }
            ]
          }
        }
      ]
    }'
);

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline(
    'db',
    '{
      "aggregate": "tenantCollection",
      "pipeline": [
        {
          "$lookup": {
            "from": "candidateRequest",
            "localField": "_id",
            "foreignField": "tenantId",
            "as": "requests",
            "pipeline": [
              { "$match": { "status": { "$ne": "unopened" } } },
              { "$project": { "status": 1, "candidateId": 1 } }
            ]
          }
        }
      ]
    }'
);
ROLLBACK;


-- Test nested lookup with multiple levels
SELECT documentdb_api.insert_one('db','users','{ "_id" : 1, "customerCode": "C100", "name": "Nimisha Doshi" }', NULL);
SELECT documentdb_api.insert_one('db','users','{ "_id" : 2, "customerCode": "C101", "name": "Parag Jain" }', NULL);

SELECT documentdb_api.insert_one('db','orders','{ "_id" : 1,  "orderId": 1, "customerCode": "C100", "productCode": "P200" }', NULL);
SELECT documentdb_api.insert_one('db','orders','{ "_id" : 2, "orderId": 2, "customerCode": "C100", "productCode": "P201" }', NULL);
SELECT documentdb_api.insert_one('db','orders','{ "_id" : 3, "orderId": 3, "customerCode": "C101", "productCode": "P200" }', NULL);

SELECT documentdb_api.insert_one('db','products','{ "_id" : 1, "productCode": "P200", "title": "Laptop", "price": 55000 }', NULL);
SELECT documentdb_api.insert_one('db','products','{ "_id" : 2, "productCode": "P201", "title": "Headphones", "price": 2000 }', NULL);

-- Create indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "users",
     "indexes": [
       {"key": {"customerCode": 1}, "name": "idx_users_customerCode"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "orders",
     "indexes": [
       {"key": {"customerCode": 1}, "name": "idx_orders_customerCode"},
       {"key": {"productCode": 1}, "name": "idx_orders_productCode"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "products",
     "indexes": [
       {"key": {"productCode": 1}, "name": "idx_products_productCode"}
     ]
   }',
   true
);

-- Execute nested lookup aggregation
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "productCode": 1
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

BEGIN;
SET LOCAL enable_seqscan TO off;
-- Now run explain verbose to check that if projection is modifying or removing doc we don't pushdown
-- 1. when join field is in inclusion projection, should pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "customerCode": 1
              }
            }
          ]
        }
      }
    ], "cursor": {} }');


-- 2. when join field is in exclusion projection, should not pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "customerCode": 0
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

-- 3. when join field is modified in projection, should not pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "customerCode": "someOtherValue"
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

-- 4. when there is mix inclusion and exclusion projection, should not pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "customerCode": 1,
                "productCode": 0
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

-- 5. when there is inclusion but join field not included, should not pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "productCode": 1
              }
            }
          ]
        }
      }
    ], "cursor": {} }');
  
-- 6. where there is exlcusion but join field not excluded, should pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "someOtherField": 0
              }
            }
          ]
        }
      }
    ], "cursor": {} }');


-- 7. lookup is on _id and _id is excluded, should pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "_id",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "_id": 0
              }
            }
          ]
        }
      }
    ], "cursor": {} }');   

-- 8. lookup is on _id and _id is included, we forcefully not inline when foreignField is _id
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "_id",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "_id": 1
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

-- 9. lookup is not on _id and _id is excluded but lookup is included,  we forcefully not inline when foreignField is _id
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "_id": 0, "customerCode" : 1
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

-- 10. lookup is not on _id and _id is included but lookup is excluded, should not pushdown
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "customerCode",
          "foreignField": "customerCode",
          "as": "requested",
          "pipeline": [
            {
              "$match": {
                "customerCode": { "$in": ["C100", "C101"] }
              }
            },
            {
              "$lookup": {
                "from": "products",
                "localField": "productCode",
                "foreignField": "productCode",
                "as": "Irequest"
              }
            },
            {
              "$project": {
                "_id": 1, "customerCode" : 0
              }
            }
          ]
        }
      }
    ], "cursor": {} }');    

ROLLBACK;

-- 11 Bug Fix 2 : Should pass foreign fields to lookupinline functions
SELECT documentdb_api.insert_one('dbAddField','users','{ "_id": 1, "name": "user1", "age": 28 }', NULL);
SELECT documentdb_api.insert_one('dbAddField','users','{ "_id": 2, "name": "user2", "age": 27 }', NULL);

SELECT documentdb_api.insert_one('dbAddField','orders','{ "_id": 101, "userId": 1, "item": "Laptop", "price": 50000 }', NULL);
SELECT documentdb_api.insert_one('dbAddField','orders','{ "_id": 102, "userId": 1, "item": "Phone", "price": 20000 }', NULL);
SELECT documentdb_api.insert_one('dbAddField','orders','{ "_id": 103, "userId": 2, "item": "Watch", "price": 5000 }', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'dbAddField',
  '{
     "createIndexes": "orders",
     "indexes": [
       {"key": {"userId": 1}, "name": "idx_orders_userId"}
     ]
   }',
   true
);

-- Buggy case which should fixed by enableUseForeignKeyLookupInline it on;
BEGIN;
SET LOCAL documentdb.enableUseForeignKeyLookupInline to off;
-- Test $addFields modifying the foreignField in lookup pipeline
SELECT document FROM bson_aggregation_pipeline('dbAddField', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "_id",
          "foreignField": "userId",
          "as": "userOrders",
          "pipeline": [
            {
              "$addFields": {
                "userId": 100
              }
            }
          ]
        }
      }
    ], "cursor": {} }');
ROLLBACK;

-- this fixes above issue
BEGIN;
SET LOCAL documentdb.enableUseForeignKeyLookupInline to on;
SELECT document FROM bson_aggregation_pipeline('dbAddField', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "_id",
          "foreignField": "userId",
          "as": "userOrders",
          "pipeline": [
            {
              "$addFields": {
                "userId": 100
              }
            }
          ]
        }
      }
    ], "cursor": {} }');

ROLLBACK;


-- Explain plan, should not pushdown as we are modifying foreignField
BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "users", "pipeline": [
      {
        "$lookup": {
          "from": "orders",
          "localField": "_id",
          "foreignField": "userId",
          "as": "userOrders",
          "pipeline": [
            {
              "$addFields": {
                "userId": 100
              }
            }
          ]
        }
      }
    ], "cursor": {} }');
ROLLBACK;




