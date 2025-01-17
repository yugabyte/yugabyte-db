-- tests specific to pushing down to the right index the $sort
-- at the moment $sort is not pushed down to index unless it is a $sort on the _id field
-- on an unsharded collection or we have a shard key filter and all the filters can be pushed to the _id index.
-- once we support sort pushdown to the index we need to revisit the strategy to push down sort on _id.
SET search_path TO helio_api,helio_api_internal,helio_api_catalog,helio_core;
SET citus.next_shard_id TO 9640000;
SET helio_api.next_collection_id TO 964000;
SET helio_api.next_collection_index_id TO 964000;

DO $$
DECLARE i int;
DECLARE a int;
DECLARE modres int;
BEGIN
FOR i IN 1..10000 LOOP
    SELECT MOD(i, 3) into modres;
    CASE
        WHEN modres = 0 THEN
            a:=12;
        WHEN modres = 1 THEN
            a:=14;
        ELSE
            a:=22;
    END CASE;
    PERFORM helio_api.insert_one('sort_pushdown', 'coll', FORMAT('{ "_id": %s, "a": %s}',  i, a)::helio_core.bson);
END LOOP;
END;
$$;

SELECT count(*) from helio_api.collection('sort_pushdown', 'coll');

-- force the analyzer to kick in to have real statistics after we did the insertion.
ANALYZE helio_data.documents_964001;

-- sort by id with no filters uses the _id_ index and returns the right results
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": 1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": 1} }');
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": -1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": -1} }');

-- filter on _id_
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 5}}, "sort": {"_id": 1}, "limit":20 }');
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 5}}, "sort": {"_id": -1}, "limit":20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 5}}, "sort": {"_id": 1} }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 5}}, "sort": {"_id": -1} }');

-- filter on a with no index
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 22}}, "sort": {"_id": 1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 22}}, "sort": {"_id": 1}, "limit": 20 }');

-- create compound index on a and _id and filter on a and on _id
SELECT helio_api_internal.create_indexes_non_concurrently('sort_pushdown', '{ "createIndexes": "coll", "indexes": [ { "key": { "a": 1, "_id": 1 }, "name": "a_id" }]}', true);

ANALYZE helio_data.documents_964001;

SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 14}}, "sort": {"_id": 1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 14}}, "sort": {"_id": 1}, "limit": 20 }');
SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 100}}, "sort": {"_id": 1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"_id": {"$gt": 100}}, "sort": {"_id": 1}, "limit": 20 }');

-- no filter should still prefer the _id index
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": 1}, "limit": 20 }');

-- shard the collection on a, should sort on object_id only when there is a shard filter.
SELECT helio_api.shard_collection('sort_pushdown', 'coll', '{"a": "hashed"}', false);
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 14}}, "sort": {"_id": 1}, "limit": 20 }');
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$gt": 14}}, "sort": {"_id": 1}, "limit": 20 }');

-- no filter on sharded collection should not sort on object_id
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {}, "sort": {"_id": 1}, "limit": 20 }');

-- drop compound index, should use the _id index
CALL helio_api.drop_indexes('sort_pushdown', '{ "dropIndexes": "coll", "index": "a_id"}');

ANALYZE helio_data.documents_964001;

EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"a": {"$eq": 14}}, "sort": {"_id": 1}, "limit": 20 }');

-- or should push down to the shards and use object_id
EXPLAIN (COSTS OFF, TIMING OFF, ANALYZE ON, SUMMARY OFF) SELECT document FROM bson_aggregation_find('sort_pushdown', '{ "find": "coll", "filter": {"$or": [{"a": {"$eq": 14}}, {"a": {"$eq": 22}}]}, "sort": {"_id": 1}, "limit": 20 }');
