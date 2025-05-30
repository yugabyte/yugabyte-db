SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 60000;
SET documentdb.next_collection_id TO 6000;
SET documentdb.next_collection_index_id TO 6000;

-- Insert data
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','orders','{ "_id" : 2, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 3, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 4, "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 6, "item" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('db','orders',' { "_id" : 7, "item" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('db','inventory',' { "_id" : 1, "sku" : "almonds", "description": "product 1", "instock" : 120 }', NULL);
SELECT documentdb_api.insert_one('db','inventory',' { "_id" : 11, "sku" : "almonds", "description": "product 1", "instock" : 240 }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 2, "sku" : "bread", "description": "product 2", "instock" : 80 }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 3, "sku" : "cashews", "description": "product 3", "instock" : 60 }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 4, "sku" : "pecans", "description": "product 4", "instock" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 5, "sku" : null, "description": "product 4", "instock" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 6, "sku" :  {"a": "x", "b" : 1, "c" : [1, 2, 3]}, "description": "complex object" }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 7, "sku" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "description": "complex array" }', NULL);
SELECT documentdb_api.insert_one('db','inventory','{ "_id" : 8, "sku" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "description": "complex array" }', NULL);



-- Test filter generation 
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') FROM documentdb_api.collection('db', 'orders');

-- Test full lookup sql
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match FROM documentdb_api.collection('db', 'orders')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Create Index on Inventory
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "inventory",
     "indexes": [
       {"key": {"sku": 1}, "name": "idx_inventory_sku"}
     ]
   }',
   true
);

-- Test Index pushdown
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','inventory');
BEGIN;
set local enable_seqscan TO off;
EXPLAIN(costs off) WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
FROM documentdb_api.collection('db', 'inventory') AS t2 
WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
ROLLBACK;

-- Insert data into a new collection to be sharded (shard key can't be an array, but can be null or object)
SELECT documentdb_api.insert_one('db','orders_sharded',' { "_id" : 1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','orders_sharded','{ "_id" : 2, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','orders_sharded',' { "_id" : 3, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','orders_sharded',' { "_id" : 4}', NULL);
SELECT documentdb_api.insert_one('db','orders_sharded',' { "_id" : 5, "item" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);

-- Shard orders collection on item 
SELECT documentdb_api.shard_collection('db','orders_sharded', '{"item":"hashed"}', false);

-- Test filter generation on sharded left collection
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') FROM documentdb_api.collection('db', 'orders_sharded');

-- Test Index pushdown on sharded left collection
BEGIN;
set local enable_seqscan TO off;
SET JIT To off;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) 
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
$Q$);
ROLLBACK;

SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'orders_sharded') order by object_id;


-- Test lookup on sharded left collection
WITH 
t1_0 AS(SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
  AS lookup_filters FROM documentdb_api.collection('db', 'orders_sharded')) 
SELECT bson_dollar_lookup_project(t1_0.document, t2_0_agg.matched_array , 'matched_docs'::text) as document 
FROM t1_0 LEFT JOIN LATERAL ( 
    WITH "LookupStage_0_0" as (SELECT * FROM documentdb_api.collection('db', 'inventory') AS t2_0 WHERE bson_dollar_in(t2_0.document, t1_0.lookup_filters) ) 
    SELECT COALESCE(array_agg("LookupStage_0_0".document::documentdb_core.bson), '{}'::bson[]) as matched_array FROM "LookupStage_0_0"
) t2_0_agg ON TRUE;

-- Test lookup on sharded left collection (rewrite avoiding the the CTE error)
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
 
-- Apply subpipeline on the out side of the join. This is a better execution plan since 
-- we are not applying the subpipeline on the entire 'from' collection
WITH simple_lookup as
(
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded'))
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
-- perform lookup
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE
) 
-- subpipeline moved down 
SELECT bson_dollar_add_fields(bson_dollar_lookup_project, '{"matched_docs.hello": "newsubpipelinefield"}') from simple_lookup;


WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded')),
-- subpipeline moved up
t2 as (SELECT bson_dollar_add_fields(document, '{"matched_docs.hello": "newsubpipelinefield"}') as document from documentdb_api.collection('db', 'inventory'))
-- now perform lookup
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Lookup subpipeline applied after the join is done (this is the mongo semantics)
WITH lookup_stage as
(
  -- Create CTE for the left/source collection (say t1)
  WITH                                                                                            
    t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	  AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded'))
  SELECT t1.document, t2_agg.document as matched
  FROM t1
  -- perform lateral join on the right collection (aka joined collection)
    LEFT JOIN LATERAL ( 
	  SELECT document
	  FROM documentdb_api.collection('db', 'inventory') AS t2 
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
t1_0 AS(SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
  AS lookup_filters FROM documentdb_api.collection('db', 'orders_sharded')) 
SELECT bson_dollar_lookup_project(t1_0.document, t2_0_agg.matched_array , 'matched_docs'::text) as document 
FROM t1_0 LEFT JOIN LATERAL ( 
    WITH "LookupStage_0_0" as (SELECT * FROM documentdb_api.collection('db', 'inventory') AS t2_0 WHERE bson_dollar_in(t2_0.document, t1_0.lookup_filters) ) 
    SELECT COALESCE(array_agg("LookupStage_0_0".document::documentdb_core.bson), '{}'::bson[]) as matched_array FROM "LookupStage_0_0"
) t2_0_agg ON TRUE;

-- Test lookup on sharded left collection (rewrite avoiding the the CTE error)
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;
 
-- Apply subpipeline on the out side of the join. This is a better execution plan since 
-- we are not applying the subpipeline on the entire 'from' collection
WITH simple_lookup as
(
WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded'))
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
-- perform lookup
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE
) 
-- subpipeline moved down 
SELECT bson_dollar_add_fields(bson_dollar_lookup_project, '{"matched_docs.hello": "newsubpipelinefield"}') from simple_lookup;


WITH                                                                                            
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded')),
-- subpipeline moved up
t2 as (SELECT bson_dollar_add_fields(document, '{"matched_docs.hello": "newsubpipelinefield"}') as document from documentdb_api.collection('db', 'inventory'))
-- now perform lookup
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Lookup subpipeline applied after the join is done (this is the mongo semantics)
WITH lookup_stage as
(
  -- Create CTE for the left/source collection (say t1)
  WITH                                                                                            
    t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"sku" : "item"}') 
	  AS match, 'mdocs' asname FROM documentdb_api.collection('db', 'orders_sharded'))
  SELECT t1.document, t2_agg.document as matched
  FROM t1
  -- perform lateral join on the right collection (aka joined collection)
    LEFT JOIN LATERAL ( 
	  SELECT document
	  FROM documentdb_api.collection('db', 'inventory') AS t2 
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
SELECT documentdb_api.insert_one('db','coll','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('db','coll','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('db','coll','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','from','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('db','from','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('db','from','{"_id": 2}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"nonex" : "a"}') FROM documentdb_api.collection('db', 'coll');
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"nonex" : "a"}') 
	AS match FROM documentdb_api.collection('db', 'coll')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'from') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Test dotted path

SELECT documentdb_api.insert_one('db','coll1','{"_id": 0, "a": 1}', NULL);
SELECT documentdb_api.insert_one('db','coll1','{"_id": 1, "a": null}', NULL);
SELECT documentdb_api.insert_one('db','coll1','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','coll1','{"_id": 3, "a": {"c": 1}}', NULL);

SELECT documentdb_api.insert_one('db','from1','{"_id": 0, "b": 1}', NULL);
SELECT documentdb_api.insert_one('db','from1','{"_id": 1, "b": null}', NULL);
SELECT documentdb_api.insert_one('db','from1','{"_id": 2}', NULL);
SELECT documentdb_api.insert_one('db','from1','{"_id": 3, "b": {"c" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','from1','{"_id": 4, "b": {"c" : 2}}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') FROM documentdb_api.collection('db', 'coll1');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') 
	AS match FROM documentdb_api.collection('db', 'coll1')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'from1') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Empty from collection tests

SELECT * FROM documentdb_api.collection('db', 'from1NonExistent');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b.c" : "a.c"}') 
	AS match FROM documentdb_api.collection('db', 'coll1')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'from1NonExistent') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Dotted path on mathched array name (some.documents)

SELECT documentdb_api.insert_one('db','coll2','{"_id": 0, "a": {"b" : 1}}', NULL);
SELECT documentdb_api.insert_one('db','coll2','{"_id": 1}', NULL);
SELECT documentdb_api.insert_one('db','from2','{"_id": 0, "target": 1}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"target" : "a.b"}') FROM documentdb_api.collection('db', 'coll2');
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"target" : "a.b"}') 
	AS match FROM documentdb_api.collection('db', 'coll2')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'same.documents'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'from2') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- Dotted path that goes through objects and arrays 

SELECT documentdb_api.insert_one('db','dottedObject','{"_id": 0, "a": [{"b": 1}, {"b": 2}]}', NULL);
SELECT documentdb_api.insert_one('db','dottedObjectFrom','{"_id": 0}', NULL);
SELECT documentdb_api.insert_one('db','dottedObjectFrom','{"_id": 1}', NULL);
SELECT documentdb_api.insert_one('db','dottedObjectFrom','{"_id": 2}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"_id" : "a.b"}') FROM documentdb_api.collection('db', 'dottedObject');

-- Dotted AS path that is already present in the left document 
SELECT documentdb_api.insert_one('db','dottedExistingAs','{"_id": 0, "a": [{"b": 1}, {"b": 2}]}', NULL);
SELECT documentdb_api.insert_one('db','dottedExistingAs','{"_id": 1, "c": [{"d": 1}, {"d": 2}]}', NULL);

SELECT bson_dollar_lookup_extract_filter_expression(document, '{"c.d" : "a.b"}') FROM documentdb_api.collection('db', 'dottedExistingAs');

WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"c.d" : "a.b"}') 
	AS match FROM documentdb_api.collection('db', 'dottedExistingAs')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'a.b'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE(array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'dottedExistingAs') AS t2 
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;

-- (B) Lookup with pipeline support

-- (B).1 Data Ingestion
SELECT documentdb_api.insert_one('db','pipelinefrom',' {"_id": 1, "name": "American Steak House", "food": ["filet", "sirloin"], "quantity": 100 , "beverages": ["beer", "wine"]}', NULL);
SELECT documentdb_api.insert_one('db','pipelinefrom','{ "_id": 2, "name": "Honest John Pizza", "food": ["cheese pizza", "pepperoni pizza"], "quantity": 120, "beverages": ["soda"]}', NULL);

SELECT documentdb_api.insert_one('db','pipelineto','{ "_id": 1, "item": "filet", "restaurant_name": "American Steak House"}', NULL);
SELECT documentdb_api.insert_one('db','pipelineto','{ "_id": 2, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "lemonade"}', NULL);
SELECT documentdb_api.insert_one('db','pipelineto','{ "_id": 3, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "soda"}', NULL);

-- (B).1 Simple Lookup with pipeline

-- {
-- 	$lookup :
-- 	{
-- 		from : pipelinefrom,
--		to:	pipelineto,
--		localField: restaurant_name,
--		foreignField: name,
--		pipeline: {
--			[
--				{
--					$match :  { quantity : { $gt, 110}}
--				}
--			]
--		}
--	}
--}

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');


-- (B).2 Index creation on the from collection

  SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "pipelinefrom",
     "indexes": [
       {"key": {"name": 1}, "name": "idx_pipelinefrom_name"},
	     {"key": {"quantity": 1}, "name": "idx_pipelinefrom_quantity"}
     ]
   }',
   true
);

-- (B).3.a Index usage
BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

-- Adding a $sort in the pipeline
BEGIN;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}, { "$sort": { "_id": 1 }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

-- (B).3.b Index usage with optimization: (if lookup has a join condition and the lookup pipeline has $match as the first 
-- stage we push the $match filter up with the join. If both conditions are one same property both the filters should be 
-- part of the index condition)

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "name" : { "$eq" : "Honest John Pizza" } }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$match": { "name" : { "$eq" : "Honest John Pizza" } }}], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

-- (B).4 Nested lookup pipeline (lookup pipeline containing lookup)

-- {
-- 	$lookup :
-- 	{
-- 		from : pipelinefrom,
--		to:	pipelineto,
--		localField: restaurant_name,
--		foreignField: name,
--		pipeline: {
--			[
--				{
--					$lookup :  {
-- 						from : pipelinefrom,
--						to:	pipelinefrom,
--						localField: _id,
--						foreignField: _id,
--						pipeline: {
--							[
--								{ unwind : "food"}
--							]
--						}
--					}
--				}
--			]
--		}
--	}
--}
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$food" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');

-- (B).5 Nested lookup pipeline (lookup pipeline containing lookup) index usage

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$unwind": "$food" } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "as": "matched_docs_id" } } ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "bar": "$food" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;



BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$sort": { "food": 1 } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;


BEGIN;
SET LOCAL enable_seqscan TO off;
EXPLAIN(costs off) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "pipelineto", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "pipeline": [ { "$lookup": { "from": "pipelinefrom", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$project": { "_id": "bar" } } ], "as": "matched_docs_id" } } ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name" }} ], "cursor": {} }');
ROLLBACK;


-- Lookup Tests for array index-based paths

SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 1, "a" : [ {"x" : 1}] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 2, "a" : {"x" : 1} }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 3, "a" : [{"x": 2}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 4, "a" : [{"y": 1}, {"x": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 5, "a" : [2, 3] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 6, "a" : {"x": [1, 2]} }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 7, "a" : [{"x": 1}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 8, "a" : [{"x": [1, 2]}, {"y": 1}] }', NULL);
SELECT documentdb_api.insert_one('db','orders1',' { "_id" : 9, "a" : [[{"x": [1, 2]}, {"y": 1}]] }', NULL);

SELECT documentdb_api.insert_one('db','inventory1',' { "_id" : 1, "b" : 1 }', NULL);

-- Test filter generation 
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.x"}'), document FROM documentdb_api.collection('db', 'orders1');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.x"}'), document FROM documentdb_api.collection('db', 'orders1');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.x.0"}'), document FROM documentdb_api.collection('db', 'orders1');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0"}'), document FROM documentdb_api.collection('db', 'orders1');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.1.y"}'), document FROM documentdb_api.collection('db', 'orders1');
SELECT bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.0.x"}'), document FROM documentdb_api.collection('db', 'orders1');
-- Test full lookup sql
WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"b" : "a.0.x"}') 
	AS match FROM documentdb_api.collection('db', 'orders1')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'inventory1') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;


WITH 
t1 AS (SELECT document, bson_dollar_lookup_extract_filter_expression(document, '{"a.0.x" : "b"}') 
	AS match FROM documentdb_api.collection('db', 'inventory1')) 
SELECT bson_dollar_lookup_project(t1.document, t2_agg.agg, 'matched_docs'::text)
FROM t1 
LEFT JOIN LATERAL ( 
	SELECT COALESCE (array_agg(t2.document::documentdb_core.bson), '{}'::bson[]) as agg 
	FROM documentdb_api.collection('db', 'orders1') AS t2
	WHERE bson_dollar_in(t2.document, t1.match)
) t2_agg ON TRUE;


-- test lookup with operator type document contents
SELECT documentdb_api.insert_one('db', 'lookup_with_operator_left', '{ "_id": 1, "a": 1, "b": { "$isArray": 1, "OtherField": 1 }}');
SELECT documentdb_api.insert_one('db', 'lookup_with_operator_right', '{ "_id": 1, "a": 1, "b": { "$isArray": 1, "OtherField": 1 }}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_with_operator_left", "pipeline": [ { "$lookup": { "from": "lookup_with_operator_right", "localField": "a", "foreignField": "a", "as": "myfoo" } }] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_with_operator_left", "pipeline": [ { "$lookup": { "from": "lookup_with_operator_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_with_operator_left", "pipeline": [ { "$lookup": { "from": "lookup_with_operator_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_with_operator_left", "pipeline": [ { "$project": { "_id": "500" }}, { "$lookup": { "from": "lookup_with_operator_right", "localField": "_id", "foreignField": "_id", "as": "myfoo" } }] }');

-- Test for Index push down crash when Foreign Field = '_id' and the target collection is sharded 
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown','{ "_id" : 2, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 3, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 4, "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 5}', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 6, "item" : {"a": "x", "b" : 1, "c" : [1, 2, 3]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_orders_pushdown',' { "_id" : 7, "item" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"] }', NULL);

SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown',' { "_id" :  "almonds", "description": "product 1", "instock" : 120 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown',' { "_id" :  "peanuts", "description": "product 1", "instock" : 240 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown','{ "_id" :  "bread", "description": "product 2", "instock" : 80 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown','{ "_id" :  "cashews", "description": "product 3", "instock" : 60 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown','{ "_id" :  "pecans", "description": "product 4", "instock" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_inventory_pushdown','{ "_id" :  "unknown", "description": "product 4", "instock" : 70 }', NULL);

SELECT documentdb_api.shard_collection('db','agg_pipeline_inventory_pushdown', '{"_id":"hashed"}', false);


SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_orders_pushdown", "pipeline": [ { "$lookup": { "from": "agg_pipeline_inventory_pushdown", "as": "matched_docs", "localField": "item", "foreignField": "_id", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN



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


BEGIN;
-- Disable optimization and test as this enabled by default now
set local documentdb.enableLookupUnwindOptimization to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id", "pipeline": [ { "$addFields": { "myBar": 1 } }, { "$limit": 10 }] } }, { "$unwind": "$matched_docs" } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "includeArrayIndex": "idx" } } ], "cursor": {} }'); -- this will not inline
	
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "orders", "pipeline": [ { "$lookup": { "from": "inventory", "as": "matched_docs", "localField": "item", "foreignField": "_id" } }, { "$unwind": { "path": "$matched_docs", "preserveNullAndEmptyArrays" : true } } ], "cursor": {} }'); -- should inline and use LEFT JOIN

ROLLBACK;