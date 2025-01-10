SET search_path TO helio_api,helio_core,helio_api_catalog;
SET helio_api.next_collection_id TO 2700;
SET helio_api.next_collection_index_id TO 2700;

-- Utility function to add multiple documents to a collection.
CREATE OR REPLACE FUNCTION insert_docs(p_db TEXT, p_coll TEXT, p_num INT, p_start INT default 0)
RETURNS void
AS $$
DECLARE
    num INTEGER := p_start;
    docText bson;
BEGIN
    WHILE num < p_num + p_start LOOP
        docText :=  CONCAT('{ "a" : ', num, '}');
        PERFORM helio_api.insert_one(p_db, p_coll, docText::helio_core.bson, NULL);
        num := num + 1;
    END LOOP;
END;
$$
LANGUAGE plpgsql;


SELECT helio_api.drop_collection('db', 'collstats1');

-- Non existing collection should return zero values
SELECT helio_api.coll_stats('db', 'collstats1');
SELECT helio_api.coll_stats('db', 'collstats1', 1024);

-- Create Collection
SELECT helio_api.create_collection('db', 'collstats1');

-- coll_stats on empty collection
SELECT helio_api.coll_stats('db', 'collstats1');
SELECT helio_api.coll_stats('db', 'collstats1', 1024);

-- Add one doc
SELECT helio_api.insert_one('db','collstats1',' { "a" : 100 }', NULL);

SET client_min_messages TO DEBUG1;

-- coll_stats on collection with single doc
SELECT helio_api.coll_stats('db', 'collstats1');
SELECT helio_api.coll_stats('db', 'collstats1', 1024);

-- Add another doc.
SELECT helio_api.insert_one('db','collstats1',' { "a" : 101 }', NULL);

-- Count should increase to 2
SELECT helio_api.coll_stats('db', 'collstats1');

-- Insert few more docs ( but keep the total num to < "coll_stats_count_policy_threshold" )
SELECT insert_docs('db', 'collstats1', 17, 0);

-- Count should be 19 (1 less than the coll_stats_count_policy_threshold)
SELECT helio_api.coll_stats('db', 'collstats1');

SET client_min_messages TO DEFAULT;


-- Create Indexes
CALL helio_api.create_indexes('db', helio_test_helpers.generate_create_index_arg('collstats1', 'index_1', '{"a.$**": 1}'));

-- "totalIndexSize" and "storageSize" fields should increase
SELECT helio_api.coll_stats('db', 'collstats1');

-- Add few more document. So that "coll_stats_count_policy_threshold" is crossed
-- Note: Adding just one extra document (above the threshold limit), may give flaky results 
-- as stats may not be precise enough
SELECT insert_docs('db', 'collstats1', 11, 100);

-- The AutoVacuum might still be napping so count in stats might still be 0 (or not updated to reflect latest inserts), 
-- so coll_stats() at this point may/may not switch it's count strategy.
-- In this test we cannot wait till nap time is over, so we manually trigger the ANALYZE
VACUUM ANALYZE helio_data.documents_3800;

-- Since the count from stats should be more than the threshold,
-- coll_stats at this point should return count (and avgObjSize) from stats (which is already updated)
SELECT helio_api.coll_stats('db', 'collstats1');

-- Insert few more docs.
SELECT insert_docs('db', 'collstats1', 10, 100);

-- The AutoVacuum might again be napping, so we manually trigger the ANALYZE
VACUUM ANALYZE helio_data.documents_3800;

-- Count should be 40 (Although its an estimate)
SELECT helio_api.coll_stats('db', 'collstats1');

SET client_min_messages TO DEFAULT;

-- Delete some documents (so that num of documents is less than threshold)
SELECT helio_api.delete('db', '{"delete":"collstats1", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');

-- In this test we cannot wait till nap time is over, so we manually trigger the ANALYZE
VACUUM ANALYZE helio_data.documents_3800;

-- The coll should now contain 17 docs, so the stats count should also be almost 17, which is less than the threshold
-- so the coll_stats should now return count values by calculating it runtime, and must be precise.
SELECT helio_api.coll_stats('db', 'collstats1');

SET client_min_messages TO DEFAULT;

-- Test with various scale factors
SELECT helio_api.coll_stats('db', 'collstats1', 1);
SELECT helio_api.coll_stats('db', 'collstats1', 2);
SELECT helio_api.coll_stats('db', 'collstats1', 2.5);
SELECT helio_api.coll_stats('db', 'collstats1', 2.99);
SELECT helio_api.coll_stats('db', 'collstats1', 100);
SELECT helio_api.coll_stats('db', 'collstats1', 1024.99);
SELECT helio_api.coll_stats('db', 'collstats1', 2147483647);     -- INT_MAX
SELECT helio_api.coll_stats('db', 'collstats1', 2147483647000);  -- More than INT_MAX

-- Shard the collection
SELECT helio_api.shard_collection('db', 'collstats1', '{"a" : "hashed"}', false);

-- Check after sharding
SELECT helio_api.coll_stats('db', 'collstats1');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { }, "count": {} } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { "scale": 10 }, "count": {} } } ] }');

---- Error cases : Scale Factor is < 1
SELECT helio_api.coll_stats('db', 'collstats1', 0);
SELECT helio_api.coll_stats('db', 'collstats1', 0.99);
SELECT helio_api.coll_stats('db', 'collstats1', -0.2);
SELECT helio_api.coll_stats('db', 'collstats1', -2);
SELECT helio_api.coll_stats('db', 'collstats1', -2147483648);    -- INT_MIN
SELECT helio_api.coll_stats('db', 'collstats1', -2147483647000); -- Less than INT_MIN

-- Clean up
SELECT helio_api.drop_collection('db', 'collstats1');

-- Should return Zero values for non-existing collection
SELECT helio_api.coll_stats('db', 'collstats1');

-- create again
SELECT helio_api.create_collection('db', 'collstats1');
-- empty table with index
CALL helio_api.create_indexes('db', helio_test_helpers.generate_create_index_arg('collstats1', 'index_1', '{"a.$**": 1}'));

-- validate both indexes show up.
SELECT helio_api.coll_stats('db', 'collstats1');

-- shard
SELECT helio_api.shard_collection('db', 'collstats1', '{"a" : "hashed"}', false);

-- validate both indexes show up.
SELECT helio_api.coll_stats('db', 'collstats1');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { }, "count": {} } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { "scale": 10 }, "count": {} } } ] }');


-- base case for agg stage
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { } } ] }');

-- invalid cases for $collStats stage
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "nonExistentColl", "pipeline": [ { "$collStats": { } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "unknownField": { }, "count": {} } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "storageStats": { "randomField": 1 }, "count": {} } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$match": { "a": 1 } }, { "$collStats": { "storageStats": { "randomField": 1 }, "count": {} } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "collstats1", "pipeline": [ { "$collStats": { "latencyStats": { }, "count": {} } } ] }');