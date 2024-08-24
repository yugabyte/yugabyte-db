SET search_path TO helio_api,helio_core,helio_api_catalog;

SET citus.next_shard_id TO 12200;
SET helio_api.next_collection_id TO 12200;
SET helio_api.next_collection_index_id TO 12200;

SELECT helio_api.insert_one('db','sales',' { "_id" : 1, "item" : "almonds", "pricing" : { "wholesale": 10, "retail": 15 }, "quantity" : 2, "year": 2020 }', NULL);
SELECT helio_api.insert_one('db','sales','{ "_id" : 2, "item" : "pecans", "pricing" : { "wholesale": 10, "retail": 9 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','sales',' { "_id" : 3, "item" : "bread", "pricing" : { "wholesale": 10, "retail": 15 }, "quantity" : 5 , "year": 2020}', NULL);
SELECT helio_api.insert_one('db','sales',' { "_id" : 4, "item" : "meat", "pricing" : { "wholesale": 4, "retail": 10 }, "quantity" : 3 , "year": 2022}', NULL);
SELECT helio_api.insert_one('db','sales','{ "_id" : 5, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','sales','{ "_id" : 6, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT helio_api.insert_one('db','sales','{ "_id" : 7, "item" : "bread", "pricing" : { "retail": 15, "wholesale": 10 }, "quantity" : 1, "year": 2020 }', NULL);

/* running multiple $addToSet accumulators with different expressions */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": { "item" : "$item" } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": "$item" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "pricingDeals": { "$addToSet": "$pricing" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$pricing.retail" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$noValue" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$addToSet": { "$getField": { "field": "a", "input": { "b": 1 } } } } } } ] }');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": { "item" : "$item" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": "$item" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "pricingDeals": { "$addToSet": "$pricing" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$pricing.retail" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$noValue" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$addToSet": { "$getField": { "field": "a", "input": { "b": 1 } } } } } } ] }');

/* running $addToSet with document that exceeds 16MB */
DO $$
DECLARE i int;
BEGIN
-- each doc is "c": 5 MB - ~5.5 MB & there's 25 of them
FOR i IN 1..25 LOOP
PERFORM helio_api.insert_one('db', 'sizes_test', FORMAT('{ "_id": %s, "groupName": "A", "c": [ %s "d" ] }', i, repeat('"' || i || repeat('a', 1000) || '", ', 5000))::helio_core.bson);
END LOOP;
END;
$$;

/* should fail with intermediate size error */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sizes_test", "pipeline": [ { "$group": { "_id": "$groupName", "data": { "$addToSet": "$c" } } } ] }');

/* shard collection */
SELECT helio_api.shard_collection('db', 'sales', '{ "_id": "hashed" }', false);

/* run same $addToSet queries to ensure consistency */
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": { "item" : "$item" } } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": "$item" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "pricingDeals": { "$addToSet": "$pricing" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$pricing.retail" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$noValue" } } } ] }');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$addToSet": { "$getField": { "field": "a", "input": { "b": 1 } } } } } } ] }');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": { "item" : "$item" } } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "itemsSold": { "$addToSet": "$item" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "pricingDeals": { "$addToSet": "$pricing" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$pricing.retail" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "retailPrices": { "$addToSet": "$noValue" } } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$addToSet": { "$getField": { "field": "a", "input": { "b": 1 } } } } } } ] }');
