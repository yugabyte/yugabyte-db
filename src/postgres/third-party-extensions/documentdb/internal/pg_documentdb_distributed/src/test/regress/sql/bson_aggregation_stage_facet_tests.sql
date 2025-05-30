SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 9610000;
SET documentdb.next_collection_id TO 961000;
SET documentdb.next_collection_index_id TO 961000;

-- Insert data
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 2, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 3, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 4, "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 5, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 6, "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 7, "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facet',' { "_id" : 8, "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);

-- Test filter generation empty input
SELECT bson_array_agg(document, 'myarray'::text) FROM documentdb_api.collection('db', 'facet1');

-- Test filter generation 
SELECT bson_array_agg(document, 'myarray'::text) FROM documentdb_api.collection('db', 'facet');

SELECT bson_object_agg(document) FROM documentdb_api.collection('db', 'facet');

-- Test full facetSQL sql
WITH "stage0" as (
  SELECT 
    documentdb_api_catalog.bson_dollar_add_fields(document, '{ "name" : { "$numberInt" : "1" } }'::bson) as document 
  FROM 
    documentdb_api.collection('db', 'facet')
), 
"stage1" as (
  WITH FacetStage AS (
    WITH "FacetStage00" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true) AS "accid", 
        BSONFIRSTONSORTED(
          bson_expression_get(document, '{ "$first" : "$quantity" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facet')
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true)
    ), 
    "FacetStage01" as (
      SELECT 
        documentdb_core.bson_repath_and_build(
          '_id' :: text, "accid", 'first':: text, "acc0"
        ) AS document 
      FROM 
        "FacetStage00"
    ), 
    "FacetStage10" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true) AS "accid", 
        BSONLASTONSORTED(
          bson_expression_get(document, '{ "$last" : "$quantity" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facet') 
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true)
    ), 
    "FacetStage11" as (
      SELECT 
        documentdb_core.bson_repath_and_build(
          '_id' :: text, "accid", 'last':: text, "acc0"
        ) AS document 
      FROM 
        "FacetStage10"
    ) 
    select 
      bson_array_agg(document :: bytea, 'facet1' :: text) as facet_row 
    from 
      "FacetStage01" 
    UNION ALL 
    select 
      bson_array_agg(document :: bytea, 'facet2' :: text) as facet_row 
    from 
      "FacetStage11"
  ) 
  SELECT 
    bson_dollar_facet_project(bson_object_agg(facet_row), true)
  FROM 
    FacetStage
) SELECT * from "stage1";


-- Test full facetSQL sql
BEGIN;
set local parallel_tuple_cost TO 0.00001;
set local parallel_setup_cost TO 0;
set local min_parallel_table_scan_size TO 0;
set local min_parallel_index_scan_size TO 0;
set local max_parallel_workers to 4;
set local max_parallel_workers_per_gather to 4;
set local max_parallel_maintenance_workers to 4;
set local enable_seqscan TO off;
SET JIT To off;
EXPLAIN(costs off)
WITH "stage0" as (
  SELECT 
    documentdb_api_catalog.bson_dollar_add_fields(document, '{ "name" : { "$numberInt" : "1" } }'::bson) as document 
  FROM 
    documentdb_api.collection('db', 'facet')
), 
"stage1" as (
  WITH FacetStage AS (
    WITH "FacetStage00" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true) AS "accid", 
        BSONFIRSTONSORTED(
          bson_expression_get(document, '{ "$first" : "$quantity" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facet')
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true)
    ), 
    "FacetStage01" as (
      SELECT 
        documentdb_core.bson_repath_and_build(
          '_id' :: text, "accid", 'first':: text, "acc0"
        ) AS document 
      FROM 
        "FacetStage00"
    ), 
    "FacetStage10" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true) AS "accid", 
        BSONLASTONSORTED(
          bson_expression_get(document, '{ "$last" : "$quantity" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facet') 
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$price" }'::bson, true)
    ), 
    "FacetStage11" as (
      SELECT 
        documentdb_core.bson_repath_and_build(
          '_id' :: text, "accid", 'last':: text, "acc0"
        ) AS document 
      FROM 
        "FacetStage10"
    ) 
    select 
      bson_array_agg(document :: bytea, 'facet1' :: text) as facet_row 
    from 
      "FacetStage01" 
    UNION ALL 
    select 
      bson_array_agg(document :: bytea, 'facet2' :: text) as facet_row 
    from 
      "FacetStage11"
  ) 
  SELECT 
    bson_dollar_facet_project(bson_object_agg(facet_row), true)
  FROM 
    FacetStage
) SELECT * from "stage1"
ROLBACK;