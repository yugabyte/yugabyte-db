SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 1012000;
SET documentdb.next_collection_id TO 10120;
SET documentdb.next_collection_index_id TO 10120;

SELECT documentdb_api.insert_one('db','sales',' { "_id" : 1, "item" : "almonds", "pricing" : { "wholesale": 10, "retail": 15 }, "quantity" : 2, "year": 2020 }', NULL);
SELECT documentdb_api.insert_one('db','sales','{ "_id" : 2, "item" : "pecans", "pricing" : { "wholesale": 4, "retail": 9 }, "quantity" : 1, "year": 2021 }', NULL);
SELECT documentdb_api.insert_one('db','sales',' { "_id" : 3, "item" : "bread", "pricing" : { "wholesale": 3, "retail": 11 }, "quantity" : 5 , "year": 2020}', NULL);
SELECT documentdb_api.insert_one('db','sales',' { "_id" : 4, "item" : "meat", "pricing" : { "wholesale": 4, "retail": 10 }, "quantity" : 3 , "year": 2022}', NULL);
SELECT documentdb_api.insert_one('db','sales','{ "_id" : 5, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "quantity" : 1, "year": 2021 }', NULL);

/* running multiple $push accumulators with different expressions */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "wholesalePricing":  { "$sum": ["$pricing.wholesale", 1] }, "qty": "$quantity"} } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "retailPricing":  { "$subtract": ["$pricing.retail", 1] }, "isBread": { "$in": ["$item", ["bread"]] } } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "shouldBeNull":  { "$subtract": ["$invalidName", 12] } } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "combinedPrice":  { "$add": ["$pricing.wholesale", "$pricing.retail"] } } } } } ] }');

/* shard collection */
SELECT documentdb_api.shard_collection('db', 'sales', '{ "_id": "hashed" }', false);

/* run same $push queries to ensure consistency */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "wholesalePricing":  { "$sum": ["$pricing.wholesale", 1] }, "qty": "$quantity"} } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "retailPricing":  { "$subtract": ["$pricing.retail", 1] }, "isBread": { "$in": ["$item", ["bread"]] } } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "shouldBeNull":  { "$subtract": ["$invalidName", 12] } } } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [ { "$group": { "_id": "$year", "items": { "$push": { "combinedPrice":  { "$add": ["$pricing.wholesale", "$pricing.retail"] } } } } } ] }');

-- Test for missing values
SELECT documentdb_api.insert_one('db','sales','{ "_id" : 7, "item" : "bread", "pricing" : { "wholesale": 75, "retail": 100 }, "year": 2021 }', NULL);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [{"$match": {"item": "bread"}}, { "$group": { "_id": "$item", "items": { "$push": { "qty": "$quantity"} } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [{"$match": {"item": "bread"}}, { "$group": { "_id": "$item", "items": { "$push": "$quantity" } } } ] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "sales", "pipeline": [{"$match": {"item": "bread"}}, { "$group": { "_id": "$item", "items": { "$push": ["$quantity"] } } } ] }');
