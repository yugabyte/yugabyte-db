SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 9610000;
SET documentdb.next_collection_id TO 961000;
SET documentdb.next_collection_index_id TO 961000;

-- Insert data
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 1, "product" : "beer", "unitPrice" : 12, "stock" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 2, "product" : "red wine", "unitPrice" : 20, "stock" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 3, "product" : "bread", "unitPrice" : 10, "stock" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 4, "product" : ["beer", "bread", "red wine"], "unitPrice" : 10, "stock" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 5, "product" : "beer", "unitPrice" : 12, "stock" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 6, "product" : "red wine", "unitPrice" : 20, "stock" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 7, "product" : "bread", "unitPrice" : 10, "stock" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','facetTest',' { "_id" : 8, "product" : ["beer", "bread", "red wine"], "unitPrice" : 10, "stock" : 5 }', NULL);

-- Test filter generation empty input
SELECT bson_array_agg(document, 'myarray'::text) FROM documentdb_api.collection('db', 'facet1');

-- Test filter generation 
SELECT bson_array_agg(document, 'myarray'::text) FROM documentdb_api.collection('db', 'facetTest');

SELECT bson_object_agg(document) FROM documentdb_api.collection('db', 'facetTest');

-- Test full facetSQL sql
WITH "stage0" as (
  SELECT 
    documentdb_api_catalog.bson_dollar_add_fields(document, '{ "name" : { "$numberInt" : "1" } }'::bson) as document 
  FROM 
    documentdb_api.collection('db', 'facetTest')
), 
"stage1" as (
  WITH FacetStage AS (
    WITH "FacetStage00" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true) AS "accid", 
        BSONFIRSTONSORTED(
          bson_expression_get(document, '{ "$first" : "$stock" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facetTest')
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true)
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
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true) AS "accid", 
        BSONLASTONSORTED(
          bson_expression_get(document, '{ "$last" : "$stock" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facetTest') 
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true)
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
    documentdb_api.collection('db', 'facetTest')
), 
"stage1" as (
  WITH FacetStage AS (
    WITH "FacetStage00" as (
      SELECT 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true) AS "accid", 
        BSONFIRSTONSORTED(
          bson_expression_get(document, '{ "$first" : "$stock" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facetTest')
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true)
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
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true) AS "accid", 
        BSONLASTONSORTED(
          bson_expression_get(document, '{ "$last" : "$stock" }'::bson, true)
        ) AS "acc0" 
      FROM 
        documentdb_api.collection('db', 'facetTest') 
      GROUP BY 
        bson_expression_get(document, '{ "_id" : "$unitPrice" }'::bson, true)
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