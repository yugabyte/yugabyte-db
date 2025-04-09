SET search_path TO documentdb_api_catalog;

SET citus.next_shard_id TO 330000;
SET documentdb.next_collection_id TO 33000;
SET documentdb.next_collection_index_id TO 33000;

DO $$
BEGIN
    FOR i IN 1..10000 LOOP
        PERFORM documentdb_api.insert_one('db', 'in_opt_tests', '{ "accid": 1, "vid": 3, "val": [1, 2] }');
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":1, "accid": 1, "vid": 1, "val": [1, 2]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":2, "accid": 1, "vid": 2, "val": [3, 4]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":3, "_id":1, "accid": 1, "vid": 1, "val": [3, 1]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":4, "accid": 1, "vid": 2, "val": {"test": 5}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":5, "accid": 1, "vid": 1, "val": [{"test": 7}]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":6, "accid": 1, "vid": 2, "val": [true, false]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":7, "accid": 1, "vid": 1, "val": 2}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":8, "accid": 1, "vid": 2, "val": 3}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":9, "accid": 1, "vid": 1, "val": 4}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":10, "accid": 1, "vid": 2, "val": [2]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":11, "accid": 1, "vid": 1, "val": [3]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":12, "accid": 1, "vid": 2, "val": [4]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":13, "accid": 1, "vid": 1, "val": [1, true]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":14, "accid": 1, "vid": 2, "val": [true, 1]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":15, "accid": 1, "vid": 1, "val": [1, 4]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":16, "accid": 1, "vid": 2, "val": [null]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":17, "accid": 1, "vid": 1, "val": null}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":18, "accid": 1, "vid": 2, "val": { "$minKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":19, "accid": 1, "vid": 1, "val": [{ "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":20, "accid": 1, "vid": 2, "val": [{ "$minKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":21, "accid": 1, "vid": 1, "val": [3, { "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":22, "accid": 1, "vid": 2, "val": { "$maxKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":23, "accid": 1, "vid": 1, "val": [{ "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":24, "accid": 1, "vid": 2, "val": [{ "$maxKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":25, "accid": 1, "vid": 1, "val": [3, { "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":26, "accid": 1, "vid": 2, "val": []}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":27, "accid": 1, "vid": 1, "a": {"val": [1, 2]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":28, "accid": 1, "vid": 2, "a": {"val": [3, 4]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":29, "accid": 1, "vid": 1, "a": {"val": [3, 1]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":30, "accid": 1, "vid": 2, "a": {"val": {"test": 5}}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":31, "accid": 1, "vid": 1, "a": {"val": [{"test": 7}]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":32, "accid": 1, "vid": 2, "a": {"val": [true, false]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":33, "accid": 1, "vid": 1, "a": {"val": 2}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":34, "accid": 1, "vid": 2, "a": {"val": 3}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":35, "accid": 1, "vid": 1, "a": {"val": 4}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":36, "accid": 1, "vid": 2, "a": {"val": [2]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":37, "accid": 1, "vid": 1, "a": {"val": [3]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":38, "accid": 1, "vid": 2, "a": {"val": [4]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":39, "accid": 1, "vid": 1, "a": {"val": [1, true]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":40, "accid": 1, "vid": 2, "a": {"val": [true, 1]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":41, "accid": 1, "vid": 1, "a": {"val": [1, 4]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":42, "accid": 1, "vid": 2, "a": {"val": [null]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":43, "accid": 1, "vid": 1, "a": {"val": null}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":44, "accid": 1, "vid": 2, "a": {"val": { "$minKey": 1 }}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":45, "accid": 1, "vid": 1, "a": {"val": [{ "$minKey": 1 }]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":46, "accid": 1, "vid": 2, "a": {"val": [{ "$minKey": 1 }, 3]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":47, "accid": 1, "vid": 1, "a": {"val": [3, { "$minKey": 1 }]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":48, "accid": 1, "vid": 2, "a": {"val": { "$maxKey": 1 }}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":49, "accid": 1, "vid": 1, "a": {"val": [{ "$maxKey": 1 }]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":50, "accid": 1, "vid": 2, "a": {"val": [{ "$maxKey": 1 }, 3]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":51, "accid": 1, "vid": 1, "a": {"val": [3, { "$maxKey": 1 }]}}', NULL);
SELECT documentdb_api.insert_one('db', 'in_opt_tests', '{"_id":52, "accid": 1, "vid": 2, "a": {"val": []}}', NULL);

SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 2, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1,  "$or": [{"val": { "$eq": 1}}, {"val": { "$eq": 3}}] } }');
 

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "in_opt_tests",
     "indexes": [
       {"key": {"val": 1, "accid": 1, "vid": 1}, "name": "val_accid_vid"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "in_opt_tests",
     "indexes": [
       {"key": {"a.val": 1, "accid": 1, "vid": 1}, "name": "a_val_accid_vid"}
     ]
   }',
   true
);

BEGIN;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "$or": [{"val": { "$eq": 1}}, {"val": { "$eq": 3}}] } }'); 
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": {"$or": [{"val": { "$eq": 1}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}, {"val": { "$eq": 3}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}] } }'); 
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [1, 3]} } }');

-- no optimization
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": []} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": [4, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "a.val": { "$in": [4, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [3]} } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "$or": [{"val": { "$eq": 1}}, {"val": { "$eq": 3}}] } }'); 
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": {"$or": [{"val": { "$eq": 1}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}, {"val": { "$eq": 3}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}] } }'); 
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [1, 3]} } }');

-- no optimization
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": []} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": [4, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "a.val": { "$in": [4, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [3]} } }');
END;

SELECT documentdb_api.shard_collection('db','in_opt_tests', '{"accid":"hashed"}', false);

BEGIN;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "$or": [{"val": { "$eq": 1}}, {"val": { "$eq": 3}}] } }'); 
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": {"$or": [{"val": { "$eq": 1}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}, {"val": { "$eq": 3}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}] } }'); 
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [1, 3]} } }');

-- no optimization
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": []} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": [4, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "a.val": { "$in": [4, 3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [3]} } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [3]} } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "$or": [{"val": { "$eq": 1}}, {"val": { "$eq": 3}}] } }'); 
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": {"$or": [{"val": { "$eq": 1}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}, {"val": { "$eq": 3}, "accid":{ "$eq": 1}, "vid": { "$eq": 1}}] } }'); 
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [1, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [1, 3]} } }');

-- no optimization
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": []} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "val": { "$in": [4, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "a.val": { "$in": [4, 3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "val": { "$in": [3]} } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "in_opt_tests", "filter": { "accid":1, "vid": 1, "a.val": { "$in": [3]} } }');
END;