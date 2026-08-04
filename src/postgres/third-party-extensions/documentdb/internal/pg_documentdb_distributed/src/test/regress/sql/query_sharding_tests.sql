SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 739000;
SET documentdb.next_collection_id TO 7390;
SET documentdb.next_collection_index_id TO 7390;


SELECT documentdb_api.insert_one('qst', 'single_shard', '{ "_id": 1, "a": 1, "b": 2, "c": 3, "d": 4 }');
SELECT documentdb_api.insert_one('qst', 'comp_shard', '{ "_id": 1, "a": 1, "b": 2, "c": 3, "d": 4 }');

SELECT documentdb_api.shard_collection('qst', 'single_shard', '{ "a": "hashed" }', false);
SELECT documentdb_api.shard_collection('qst', 'comp_shard', '{ "a": "hashed", "b": "hashed", "c": "hashed" }', false);

-- cross shard queries
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "e": 1 } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "e": 1 } } ] }');

-- single shard queries
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "e": 1, "a": 4 } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "e": 1, "a": 4, "b": 2, "c": 6 } } ] }');

-- single shard with and
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$and": [ { "e": 1 }, { "a": 4 } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "e": 1 }, { "a": 4 }, { "b": 2 }, { "c": 6 } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "e": 1, "a": 4 }, { "b": 2, "c": 6 } ] } } ] }');

-- all components speciied but not equals, cross-shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$and": [ { "e": 1 }, { "a": { "$ne": 4 } } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "e": 1, "a": 4 }, { "b": 2, "c": { "$gt": 6 } } ] } } ] }');

-- $or with a single branch - single shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$or": [ { "a": 4 } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$or": [ { "e": 1, "a": 4, "b": 2, "c": 6 } ] } } ] }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "b": 2, "$or": [ { "e": 1, "a": 4, "c": 6 } ] } } ] }');

-- top level $or - N shard query but not all branches have shard key: cross-shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$or": [ { "e": 1 }, { "a": 4 } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$or": [ { "e": 1, "a": 4, "b": 2, "c": 6 }, { "a": 4, "b": 5 } ] } } ] }');

-- top level $or - all branches have shard key - N shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$or": [ { "e": 1, "a": 5 }, { "a": 4 } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$or": [ { "e": 1, "a": 4, "b": 2, "c": 6 }, { "a": 4, "b": 5, "c": 9 } ] } } ] }');

-- $and  and $or with $and having shard key and $or having shard key - single shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$and": [ { "a": 15}, { "$or": [ { "e": 1, "a": 5 }, { "a": 4 } ] } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "a": 15, "b": 3, "c": 7, "g": 5 }, { "$or": [ { "e": 1, "a": 4, "b": 2, "c": 6 }, { "a": 4, "b": 5, "c": 9 } ] } ] } } ] }');

-- $and and $or with $or having shard key - N shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$and": [ { "g": 15}, { "$or": [ { "e": 1, "a": 5 }, { "a": 4 } ] } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "g": 5 }, { "$or": [ { "e": 1, "a": 4, "b": 2, "c": 6 }, { "a": 4, "b": 5, "c": 9 } ] } ] } } ] }');


-- $and and $or with $or not having shard key - cross-shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "$and": [ { "g": 15}, { "$or": [ { "e": 1}, { "a": 4 } ] } ] } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "$and": [ { "g": 5 }, { "$or": [ { "e": 1, "a": 4, "c": 6 }, { "a": 4, "b": 5, "c": 9 } ] } ] } } ] }');

-- test all the same above but with $in instead of $or
-- $in with a single branch - single shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4 ] } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4 ] }, "b": { "$in": [ 2 ] }, "c": { "$in": [ 6 ] } } } ] }');

-- $in with multiple branches - cross shard but limited subset of shards
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "single_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4, 5 ] } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4, 5 ] }, "b": { "$in": [ 2, 3 ] }, "c": { "$in": [ 6, 7 ] } } } ] }');

-- $in with mixed equality $in
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$eq": 5 }, "b": { "$in": [ 2, 3 ] }, "c": { "$in": [ 6, 6 ] } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$eq": 5 }, "b": { "$in": [ 2, 3 ] }, "c": { "$eq": 6 } } } ] }');

-- $in but not all are specified - full cross-shard
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4, 5 ] }, "c": { "$in": [ 6, 6 ] } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('qst', '{ "aggregate": "comp_shard", "pipeline": [ { "$match": { "a": { "$in": [ 4, 5 ] }, "b": { "$gte": 3 }, "c": { "$in": [ 6, 7 ] } } } ] }');
