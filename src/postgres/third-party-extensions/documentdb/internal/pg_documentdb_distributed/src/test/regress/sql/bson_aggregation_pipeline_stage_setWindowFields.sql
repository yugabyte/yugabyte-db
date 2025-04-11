SET search_path TO documentdb_api_catalog;

SET citus.next_shard_id TO 455000;
SET documentdb.next_collection_id TO 4550;
SET documentdb.next_collection_index_id TO 4550;

SELECT documentdb_api.drop_collection('db','setWindowFields');

-- Add error and validation tests for setWindowFields stage

SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 10, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "def", "cost": 8, "quantity": 502, "date": { "$date": { "$numberLong": "1718841605000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "ghi", "cost": 4, "quantity": 503, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 10, "quantity": 504, "date": { "$date": { "$numberLong": "1718841615000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "def", "cost": 8, "quantity": 505, "date": { "$date": { "$numberLong": "1718841620000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "ghi", "cost": 4, "quantity": 506, "date": { "$date": { "$numberLong": "1718841630000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 10, "quantity": 507, "date": { "$date": { "$numberLong": "1718841640000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "def", "cost": 8, "quantity": 508, "date": { "$date": { "$numberLong": "1718841680000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 9, "a": "ghi", "cost": 4, "quantity": 509, "date": { "$date": { "$numberLong": "1718841700000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 10, "a": "abc", "cost": 10, "quantity": 510, "date": { "$date": { "$numberLong": "1718841800000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 11, "a": "def", "cost": 8, "quantity": 511, "date": { "$date": { "$numberLong": "1718841900000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 12, "a": "ghi", "cost": 4, "quantity": 512, "date": { "$date": { "$numberLong": "1718842600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 13, "a": "abc", "cost": 80, "quantity": 515, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);

SELECT documentdb_api.insert_one('db','setWindowFields_invalidtypes','{ "_id": 1, "a": "abc", "cost": 80, "quantity": 515, "date": 1 }', NULL); -- date type mismatch
SELECT documentdb_api.insert_one('db','setWindowFields_invalidtypes','{ "_id": 2, "a": "abc", "cost": 80, "quantity": 515 }', NULL); -- date is missing
SELECT documentdb_api.insert_one('db','setWindowFields_invalidtypes','{ "_id": 3, "a": "abc", "cost": "80", "quantity": 515 }', NULL); -- cost type mismatch
SELECT documentdb_api.insert_one('db','setWindowFields_invalidtypes','{ "_id": 4, "a": "abc", "quantity": 515 }', NULL); -- cost is missing

-- Validations of $setWindowFields stage and negative test cases
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": "Not an object" }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "partitionBy": 1, "sortBy": {"a": 1} } }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$sum": 1} }, "unknownField": {} } }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$sum": 1} }, "partitionBy": [1] } }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$sum": 1} }, "partitionBy": { "$concatArrays": [[1], [2]] }}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$sum": 1} }, "partitionBy": 1, "sortBy": { "a" : "asc" }}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$sum": 1} }, "partitionBy": 1, "sortBy": "Not an Object"}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": "Not an object" }}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total": {"$unknownOperator": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "$total": {"$sum": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "": {"$unknownOperator": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1}, "total2": "Not an object" }}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": "Not an object"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "unknownBound": [1, 2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "": [1, 2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": "not an array" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "range": { "a": 1, "b": 2 } }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["current", "current"], "unknownBound": [1, 2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "range": ["current", "current"], "unknownBound": [1, 2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["current", "current"], "unit": "second" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [-1 , 1] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["current" , 1] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["current" , "current"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["current" , "unbounded"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["unbounded" , 1] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [-1 , "unbounded"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": ["" , ""] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "range": ["unbounded" , "unbounded"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"a": 1, "b": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["unbounded" , "unbounded"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"a": -1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["unbounded" , "unbounded"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [1] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [1, 2, 3] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [1, "text"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [1, {"a": 1}] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "documents": [1, -1] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "range": [1, "current"] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "output": { "total1": {"$sum": 1 , "window": { "range": ["current", -2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"date": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2] }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"a": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2], "unit": "day" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"a": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2], "unit": "days" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": { "sortBy": {"a": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2], "unit": "Day" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$match": {"_id": 1 }}, {"$setWindowFields": { "sortBy": {"date": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$match": { "_id": 2 }}, {"$setWindowFields": { "sortBy": {"date": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$match": { "_id": 3 }}, {"$setWindowFields": { "sortBy": {"cost": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$match": { "_id": 4 }}, {"$setWindowFields": { "sortBy": {"cost": 1}, "output": { "total1": {"$sum": 1 , "window": { "range": ["current", 2]}}}}}]}');

-- Common Positive tests for $setWindowFields stage and explains

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"total": { "$sum": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"output": {"total": { "$sum": 1}}}}]}'); -- No partitionBy or sortBy
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": {"a": 1, "quantity": -1}, "output": {"total": { "$sum": 1}}}}]}'); -- No partitionBy

-- [Optimization] Partition by on same shard key pushes the $setWindowFields stage to the shard
SELECT documentdb_api.insert_one('db','setWindowFields_sharded','{ "_id": 1, "a": "abc", "cost": 10, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_sharded','{ "_id": 2, "a": "def", "cost": 8, "quantity": 502, "date": { "$date": { "$numberLong": "1718841605000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_sharded','{ "_id": 3, "a": "ghi", "cost": 4, "quantity": 503, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);

SELECT documentdb_api.shard_collection('db', 'setWindowFields_sharded', '{"a": "hashed"}', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": { "partitionBy": "$cost", "output": {"total": { "$sum": 1}}}}]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": { "partitionBy": "$cost", "output": {"total": { "$sum": 1}}}}]}'); -- different partitionby, not pushed to shards

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": { "partitionBy": "$a", "output": {"total": { "$sum": 1}}}}]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": { "partitionBy": "$a", "output": {"total": { "$sum": 1}}}}]}'); -- same partitioBy as shardkey, pushed to shards

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$match": {"a": "def"}}, {"$setWindowFields": { "output": {"total": { "$sum": 1}}}}]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$match": {"a": "def"}}, {"$setWindowFields": { "output": {"total": { "$sum": 1}}}}]}'); -- target a single shard with $match, pushed to shard

-- Push to shard doesn't work when there is multi key shard key
SELECT documentdb_api.shard_collection('db', 'setWindowFields_sharded', '{"a": "hashed", "b": "hashed"}', true);
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": { "partitionBy": "$a", "output": {"total": { "$sum": 1}}}}]}'); -- same partitioBy as first key in shardkey, not pushed to shards


-----------------------------------------------------------
-- $sum accumulator tests
-----------------------------------------------------------
----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$sum": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": [0, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": [-10, 10]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["current", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["unbounded", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"totalQuantity": { "$sum": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"totalQuantity": { "$sum": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": [-5, 5], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": [-5, 5], "unit": "day"}}}}}]}');
-- current in range doesn't mean current row its always the first peer which has same sort by value. This differs from MongoDB
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$sum": 1, "window": {"range": ["current", "current"], "unit": "day"}}}}}]}');


-----------------------------------------------------------
-- $count accumulator tests
-----------------------------------------------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "{}", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalCount": { "$count": {}}}}}]}');

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalCount": { "$count": {}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": [0, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": [-10, 10]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["current", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", -1]}}}}}]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": ["unbounded", "current"]}}}}}]}');
    SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-5, 5], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-5, 5], "unit": "day"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": ["current", "current"], "unit": "day"}}}}}]}');


-- Should error since $count take no argument
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"countInGroup": { "$count": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"countInGroup": { "$count": "str"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"countInGroup": { "$count": true }}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"countInGroup": { "$count": {"fst": 1} }}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"countInGroup": { "$count": [1, "$a"] }}}}]}');

-- test on shard collection
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalCount": { "$count": {}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalCount": { "$count": {}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": [-5, 5], "unit": "day"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"countInGroup": { "$count": {}, "window": {"range": ["current", "current"], "unit": "day"}}}}}]}');

-- test on shard collection with range window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"sortBy": {"quantity": 1}, "output": {"totalCount": { "$count": {}, "window": {"range": [-0.5, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"sortBy": {"quantity": 1}, "output": {"totalCount": { "$count": {}, "window": {"range": [-1, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_sharded", "pipeline":  [{"$setWindowFields": {"sortBy": {"quantity": 1}, "output": {"totalCount": { "$count": {}, "window": {"range": [-1, 1]}}}}}]}');

-----------------------------------------------------------
-- $avg accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$avg": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$avg": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$avg": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"totalQuantity": { "$avg": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"totalQuantity": { "$avg": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"totalQuantity": { "$avg": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$avg": "$quantity", "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$avg": "$quantity", "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');


-----------------------------------------------------------
-- $push accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$push": "$$varRef", "window": {"documents": ["unbounded", "unbounded"]}}}}}], "let": {"varRef": "Hello"}}');
----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"totalQuantity": { "$push": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$match": {"quantity": {"$type": "number"}}}, {"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$push": "$quantity", "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$push": "$quantity", "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');

---------------------
-- Missing Field Test
---------------------

SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 1, "a": "abc", "quantity": 30, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 2, "a": "abc", "quantity": [], "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 3, "a": "abc", "quantity": {}, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 4, "a": "abc", "quantity": null, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 5, "a": "abc", "quantity": { "$undefined": true }, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_push_missingFields','{ "_id": 6, "a": "abc", "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);

-- Should push every quantity except a NULL from _id: 6 document where quantity is missing
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_push_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$push": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_push_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$push": { "qty": "$quantity" }, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_push_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$push": [ "$quantity" ], "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_push_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$push": "$non_existing" } } } } ]}');


-----------------------------------------------------------
-- $addToSet accumulator tests
-----------------------------------------------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": 1}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": [1]}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "{}", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": []}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "{}", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": {}}}}}]}');

----------------------
-- Document window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"addedSet": { "$addToSet": ["$$varRef1", "$$varRef2"], "window": {"documents": ["unbounded", "unbounded"]}}}}}], "let": {"varRef1": "Hello", "varRef2": "World"}}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"addedSet": { "$addToSet": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$match": {"quantity": {"$type": "number"}}}, {"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$addToSet": "$quantity", "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$addToSet": "$quantity", "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');

---------------------
-- Missing Field Test
---------------------

SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 1, "a": "abc", "quantity": 30, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 2, "a": "abc", "quantity": [], "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 3, "a": "abc", "quantity": {}, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 4, "a": "abc", "quantity": null, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 5, "a": "abc", "quantity": { "$undefined": true }, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields_addToSet_missingFields','{ "_id": 6, "a": "abc", "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);

-- Should addToSet every quantity except a NULL from _id: 6 document where quantity is missing
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_addToSet_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"addedSet": { "$addToSet": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_addToSet_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"addedSet": { "$addToSet": { "qty": "$quantity" }, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_addToSet_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"addedSet": { "$addToSet": [ "$quantity" ], "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_addToSet_missingFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"addedSet": { "$addToSet": "$non_existing" } } } } ]}');

-----------------------------------------------------------
-- $covariancePop & $covarianceSamp accumulator tests
-----------------------------------------------------------

-- empty collection
SELECT documentdb_api.insert_one('db','empty_covar_col',' { "_id": 0, "x": 1 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_covar_col", "pipeline":  [{"$setWindowFields": {"output": {"covariancePop": { "$covariancePop": ["$x", "$y"]}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_covar_col", "pipeline":  [{"$setWindowFields": {"output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"]}}}}]}');

-- collection with single document
SELECT documentdb_api.insert_one('db','single_col',' { "_id": 0, "x": 1, "y": 2 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_col", "pipeline":  [{"$setWindowFields": {"output": {"covariancePop": { "$covariancePop": ["$x", "$y"]}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_col", "pipeline":  [{"$setWindowFields": {"output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"]}}}}]}');

-- document with non-numeric values
SELECT documentdb_api.insert_one('db','non_numeric_col',' { "_id": 0, "x": "a", "y": "b" }');
SELECT documentdb_api.insert_one('db','non_numeric_col',' { "_id": 1, "x": "c", "y": "d" }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "non_numeric_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "non_numeric_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with mixed values
SELECT documentdb_api.insert_one('db','mixed_col',' { "_id": 0, "x": 1, "y": "a" }');
SELECT documentdb_api.insert_one('db','mixed_col',' { "_id": 1, "x": 2, "y": 0 }');
SELECT documentdb_api.insert_one('db','mixed_col',' { "_id": 2, "x": 3, "y": "b" }');
SELECT documentdb_api.insert_one('db','mixed_col',' { "_id": 3, "x": 4, "y": 1 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mixed_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mixed_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with NAN values
SELECT documentdb_api.insert_one('db','nan_col',' { "_id": 0, "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','nan_col',' { "_id": 1, "x": 2, "y": 3 }');
SELECT documentdb_api.insert_one('db','nan_col',' { "_id": 2, "x": 3, "y": {"$numberDecimal": "NaN" } }');
SELECT documentdb_api.insert_one('db','nan_col',' { "_id": 3, "x": 4, "y": 4 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nan_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nan_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with Infinity values
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 0, "type": "a", "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 1, "type": "a", "x": 2, "y": {"$numberDecimal": "Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 2, "type": "a", "x": 3, "y": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 3, "type": "a", "x": 4, "y": 4 }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 5, "type": "b", "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 6, "type": "b", "x": 2, "y": 3 }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 7, "type": "b", "x": 3, "y": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col',' { "_id": 8, "type": "b", "x": 4, "y": {"$numberDecimal": "Infinity" } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with Decimal128 values
SELECT documentdb_api.insert_one('db','decimal_col',' { "_id": 0, "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','decimal_col',' { "_id": 1, "x": 2, "y": 3 }');
SELECT documentdb_api.insert_one('db','decimal_col',' { "_id": 2, "x": 3, "y": {"$numberDecimal": "4.0" } }');
SELECT documentdb_api.insert_one('db','decimal_col',' { "_id": 3, "x": 4, "y": 5 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "decimal_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "decimal_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- number overflow
SELECT documentdb_api.insert_one('db','overflow_col',' { "_id": 0, "x": 1, "y": {"$numberDecimal": "100000004"} }');
SELECT documentdb_api.insert_one('db','overflow_col',' { "_id": 1, "x": 2, "y": {"$numberDecimal": "10000000007"} }');
SELECT documentdb_api.insert_one('db','overflow_col',' { "_id": 2, "x": 3, "y": {"$numberDecimal": "1000000000000013"} }');
SELECT documentdb_api.insert_one('db','overflow_col',' { "_id": 3, "x": 4, "y": {"$numberDecimal": "1000000000000000000"} }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "overflow_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "overflow_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- long values return double
SELECT documentdb_api.insert_one('db','long_col',' { "_id": 0, "x": 1, "y": {"$numberLong": "10000000004"} }');
SELECT documentdb_api.insert_one('db','long_col',' { "_id": 1, "x": 2, "y": {"$numberLong": "10000000007"} }');
SELECT documentdb_api.insert_one('db','long_col',' { "_id": 2, "x": 3, "y": {"$numberLong": "10000000013"} }');
SELECT documentdb_api.insert_one('db','long_col',' { "_id": 3, "x": 4, "y": {"$numberLong": "10000000016"} }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
-- evaluate data in expression
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", {"$multiply": ["$x", "$y"]}], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", {"$multiply": ["$x", "$y"]}], "window": { "documents": ["unbounded", "current"]}}}}}]}');


-- negative cases
-- incorrect arguments won't throw error
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y", "$z"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": [5], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["a", {}], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": 3, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": {"a": "$x"}, "window": { "documents": ["unbounded", "current"]}}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y", "$z"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": [5], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["a", {}], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": 3, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": {"a": "$x"}, "window": { "documents": ["unbounded", "current"]}}}}}]}');


SELECT documentdb_api.insert_one('db','window_col',' { "_id": 0, "x": 1, "y": 2, "type": "a", "date": { "$date": { "$numberLong": "1718841600000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 1, "x": 2, "y": 3, "type": "a", "date": { "$date": { "$numberLong": "1718841605000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 2, "x": 3, "y": 4, "type": "a", "date": { "$date": { "$numberLong": "1718841610000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 3, "x": 4, "y": 5, "type": "a", "date": { "$date": { "$numberLong": "1718841615000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 4, "x": 5, "y": 6, "type": "a", "date": { "$date": { "$numberLong": "1718841620000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 5, "x": 6, "y": 7, "type": "a", "date": { "$date": { "$numberLong": "1718841630000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 6, "x": 7, "y": 8, "type": "b", "date": { "$date": { "$numberLong": "1718841640000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 7, "x": 8, "y": 9, "type": "b", "date": { "$date": { "$numberLong": "1718841680000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 8, "x": 9, "y": 10, "type": "b", "date": { "$date": { "$numberLong": "1718841700000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 9, "x": 10, "y": 11, "type": "b", "date": { "$date": { "$numberLong": "1718841800000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 10, "x": 11, "y": 12, "type": "b", "date": { "$date": { "$numberLong": "1718841900000" } } }');
SELECT documentdb_api.insert_one('db','window_col',' { "_id": 11, "x": 12, "y": 13, "type": "b", "date": { "$date": { "$numberLong": "1718842600000" } } }');

-- without window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"]}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"]}}}}]}');

-- document window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

-- range window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": {"range": [-2, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": {"range": [-3, 3], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"range": [-2, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"range": [-3, 3], "unit": "minute"}}}}}]}');

-- unsharded collection
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": { "date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["unbounded", "current"]}}}}}]}');

-- shard collection
SELECT documentdb_api.shard_collection('db', 'window_col', '{ "type": "hashed" }', false);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": { "date": 1}, "output": {"covariancePop": { "$covariancePop": ["$x", "$y"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": {"date": 1}, "output": {"covarianceSamp": { "$covarianceSamp": ["$x", "$y"], "window": {"documents": ["unbounded", "current"]}}}}}]}');

-----------------------------------------------------------
-- $rank accumulator tests
-----------------------------------------------------------

-- no values overlap all unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1} ,"output": {"rank": { "$rank": {}}}}}]}');

-- values overlap non-unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": {}}}}}]}');

-- field not present sort by field and also heterogenous bson type
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": {}}}}}]}');

-- error scenario window provided in input.
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": {}, "window": []}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": {}, "window": {"documents": [-1, 1]}}}}}]}');

-- error when input is not empty document to $rank
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": {"a":1}}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rank": { "$rank": "$a" }}}}]}');

-- error when input does not have a sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a" ,"output": {"rank": { "$rank": {} }}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rank": { "$rank": {} }}}}]}');

-- error when window is present > rank input valiation > sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rank": { "$rank": {"a":1}, "window":[] }}}}]}');

-- error when rank is not having empty document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rank": { "$rank": {"a":1} }}}}]}');

-- error when sort is not as expected
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rank": { "$rank": {} }}}}]}');


-----------------------------------------------------------
-- $denseRank accumulator tests
-----------------------------------------------------------

-- no values overlap all unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1} ,"output": {"denseRank": { "$denseRank": {}}}}}]}');

-- values overlap non-unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": {}}}}}]}');

-- field not present sort by field and also heterogenous bson type
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": {}}}}}]}');

-- error scenario window provided in input.
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": {}, "window": []}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": {}, "window": {"documents": [-1, 1]}}}}}]}');

-- error when input is not empty document to $denseRank
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": {"a":1}}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"denseRank": { "$denseRank": "$a" }}}}]}');

-- error when input does not have a sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a" ,"output": {"denseRank": { "$denseRank": {} }}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"denseRank": { "$denseRank": {} }}}}]}');

-- error when window is present > denseRank input valiation > sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"denseRank": { "$denseRank": {"a":1}, "window":[] }}}}]}');

-- error when denseRank is not having empty document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"denseRank": { "$denseRank": {"a":1} }}}}]}');

-- error when sort is not as expected
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"denseRank": { "$denseRank": {} }}}}]}');

-----------------------------------------------------------
-- $documentNumber accumulator tests
-----------------------------------------------------------

-- no values overlap all unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1} ,"output": {"rowNumber": { "$documentNumber": {}}}}}]}');

-- values overlap non-unique values for sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": {}}}}}]}');

-- field not present sort by field and also heterogenous bson type
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": {}}}}}]}');

-- error scenario window provided in input.
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": {}, "window": []}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": {}, "window": {"documents": [-1, 1]}}}}}]}');

-- error when input is not empty document to $documentNumber
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": {"a":1}}}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1} ,"output": {"rowNumber": { "$documentNumber": "$a" }}}}]}');

-- error when input does not have a sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a" ,"output": {"rowNumber": { "$documentNumber": {} }}}}]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rowNumber": { "$documentNumber": {} }}}}]}');

-- error when window is present > rowNumber input valiation > sort by clause
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rowNumber": { "$documentNumber": {"a":1}, "window":[] }}}}]}');

-- error when rowNumber is not having empty document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rowNumber": { "$documentNumber": {"a":1} }}}}]}');

-- error when sort is not as expected
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields_invalidtypes", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"cost": 1, "a":-1} ,"output": {"rowNumber": { "$documentNumber": {} }}}}]}');

-----------------------------------------------------------
-- $integral and $derivative accumulator tests
-----------------------------------------------------------
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814630000" }}, "a": "abc", "kilowatts": 2.95}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814660000" }}, "a": "abc", "kilowatts": 2.7}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814690000" }}, "a": "abc", "kilowatts": 2.6}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814720000" }}, "a": "abc", "kilowatts": 2.98}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "2", "timeStamp": { "$date": { "$numberLong": "1589814630000" }}, "a": "abc", "kilowatts": 2.5}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "2", "timeStamp": { "$date": { "$numberLong": "1589814660000" }}, "a": "abc", "kilowatts": 2.25}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "2", "timeStamp": { "$date": { "$numberLong": "1589814690000" }}, "a": "abc", "kilowatts": 2.75}', NULL);
SELECT documentdb_api.insert_one('db', 'powerConsumption', '{"powerMeterID": "2", "timeStamp": { "$date": { "$numberLong": "1589814720000" }}, "a": "abc", "kilowatts": 2.82}', NULL);
-- Validations
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "minute" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -10, 20 ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "millisecond" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "second" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "minute" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "day" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "week" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- Negative tests
-- unknown argument
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour", "unknown": 1 }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral (with no 'unit')
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral (with no 'sortBy')
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral sort by unknown field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "unknown": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral with invalid 'unit'
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "year" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral with no input
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral with invalid input
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$unknown", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
-- $integral with invalid window range
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -1, "current" ] } } } } }, { "$unset": "_id" } ]}');
-- $integral input is str
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$str", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');

-- test integral without timestamp
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 1, "x": 0, "y": 1}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 1, "x": 1, "y": 2}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 1, "x": 2, "y": 1}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 1, "x": 3, "y": 4}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 2, "x": 0, "y": 100}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 2, "x": 2, "y": 105}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 2, "x": 4, "y": 107}', NULL);
SELECT documentdb_api.insert_one('db', 'collNumeric', '{"partitionID": 2, "x": 6, "y": -100}', NULL);
-- Validations
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 2 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 2 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ "unbounded", "current" ]} } } } }, { "$unset": "_id" } ]}');
-- Negative tests, common negative tests for $integral
-- $integral (with unit and without timestamp)
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y", "unit": "hour" }, "window": { "range": [ -1, 2 ]} } } } }, { "$unset": "_id" } ]}'); 

-- test derivative with timestamp
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "miles": 1295.1, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814660000" } }, "miles": 1295.63, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814690000" } }, "miles": 1296.25, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814720000" } }, "miles": 1296.76, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "miles": 10234.1, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814660000" } }, "miles": 10234.33, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814690000" } }, "miles": 10234.73, "a": "abc"}', NULL);
SELECT documentdb_api.insert_one('db', 'deliveryFleet', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814720000" } }, "miles": 10235.13, "a": "abc"}', NULL);

-- Validations
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "millisecond" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "second" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "minute" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "day" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "week" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');

-- Negative tests
-- $derivative (with no 'unit')
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative with invalid 'unit'
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "month" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative (with no 'sortBy')
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative (with invalid str 'sortBy')
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "str": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative sort by unknown field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "unknown": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative with no window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative with invalid input
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$str", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- $derivative with no input
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
-- unknown argument
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour", "unknown": 1 }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');

-- test derivative without timestamp
-- Validations
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y"}, "window": { "range": [ "unbounded", "current" ]} } } } }, { "$unset": "_id" } ]}');
-- Negative tests
-- $derivative (with 'unit' and without timestamp)
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y", "unit": "hour" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
-- $derivative (window is with date range)
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ], "unit": "second"} } } } }, { "$unset": "_id" } ]}');

-- test integral and derivative with shard
SELECT documentdb_api.shard_collection('db', 'collNumeric', '{ "type": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'deliveryFleet', '{ "type": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'powerConsumption', '{ "type": "hashed" }', false);

-- derivative
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "derivative": { "$derivative": { "input": "$y"}, "window": { "range": [ "unbounded", "current" ]} } } } }, { "$unset": "_id" } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "deliveryFleet", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "second" } } } } }, { "$match": { "truckAverageSpeed": { "$gt": 50 } } }, { "$unset": "_id" } ]}');

-- integral
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 2 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 2 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNumeric", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ "unbounded", "current" ]} } } } }, { "$unset": "_id" } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -1, 2 ], "unit": "minute" } } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerConsumption", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ -10, 20 ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');

-- test integral and derivative with invalid mixed types
-- insert non-date data
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "miles": 1295.1}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814660000" } }, "miles": 1295.63}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814690000" } }, "miles": 1296.25}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "1", "timeStamp": { "$date": { "$numberLong": "1589814720000" } }, "miles": 1296.76}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "miles": 10234.1}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814660000" } }, "miles": 10234.33}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814690000" } }, "miles": 10234.73}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "2", "timeStamp": { "$date": { "$numberLong": "1589814720000" } }, "miles": 10235.13}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "2", "timeStamp": 0, "miles": 10234.73}', NULL);
SELECT documentdb_api.insert_one('db', 'IntegralInvalid', '{"truckID": "3", "timeStamp": 1, "miles": 10234.73}', NULL);
-- derivative failed
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "IntegralInvalid", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$derivative": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');
-- integral failed
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{"aggregate": "IntegralInvalid", "pipeline": [ { "$setWindowFields": { "partitionBy": "$truckID", "sortBy": { "timeStamp": 1 }, "output": { "truckAverageSpeed": { "$integral": { "input": "$miles", "unit": "hour" }, "window": { "range": [ -30, 0 ], "unit": "second" } } } } }, { "$unset": "_id" } ]}');

-- test integral and derivative with infinity/null/NaN mixed types
SELECT documentdb_api.insert_one('db', 'collNaN', '{"partitionID": 1, "x": 0, "y":  {"$numberDecimal": "NaN" }}', NULL);
-- test NaN
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNaN", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "collNaN", "pipeline": [ { "$setWindowFields": { "partitionBy": "$partitionID", "sortBy": { "x": 1 }, "output": { "integral": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');

-- test infinity
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 0, "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 1, "x": 2, "y": {"$numberDecimal": "Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 2, "x": 3, "y": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 3, "x": 4, "y": 4 }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 5, "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 6, "x": 2, "y": 3 }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 7, "x": 3, "y": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_col_integral',' { "_id": 8, "x": 4, "y": {"$numberDecimal": "Infinity" } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_col_integral", "pipeline": [ { "$setWindowFields": { "partitionBy": "$_id", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_col_integral", "pipeline": [ { "$setWindowFields": { "partitionBy": "$_id", "sortBy": { "x": 1 }, "output": { "integral": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');

-- test null
SELECT documentdb_api.insert_one('db','null_col',' { "_id": 0, "x": 1, "y": 2 }');
SELECT documentdb_api.insert_one('db','null_col',' { "_id": 1, "x": 2, "y": null }');
SELECT documentdb_api.insert_one('db','null_col',' { "_id": 2, "x": 3, "y": 3 }');
SELECT documentdb_api.insert_one('db','null_col',' { "_id": 3, "x": 4, "y": 4 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "null_col", "pipeline": [ { "$setWindowFields": { "partitionBy": "$_id", "sortBy": { "x": 1 }, "output": { "integral": { "$integral": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "null_col", "pipeline": [ { "$setWindowFields": { "partitionBy": "$_id", "sortBy": { "x": 1 }, "output": { "integral": { "$derivative": { "input": "$y" }, "window": { "range": [ -1, 1 ]} } } } }, { "$unset": "_id" } ]}');

-- test decimal overflow
SELECT documentdb_api.insert_one('db', 'powerMeterDataDecimal', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "kilowatts": { "$numberDecimal": "1E+320" } }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDecimal', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814690000" } }, "kilowatts": { "$numberDecimal": "2E+340" } }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDecimal', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814700000" } }, "kilowatts": { "$numberDecimal": "2E+330" } }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDecimal', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814800000" } }, "kilowatts": { "$numberDecimal": "3E+332" } }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDecimal', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814900000" } }, "kilowatts": { "$numberDecimal": "3.123E+331" } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerMeterDataDecimal", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');

-- test double overflow
SELECT documentdb_api.insert_one('db', 'powerMeterDataDouble', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814630000" } }, "kilowatts": 1e308 }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDouble', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814900000" } }, "kilowatts": 1e308 }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDouble', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814910000" } }, "kilowatts": 1e308 }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDouble', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814920000" } }, "kilowatts": 1e308 }');
SELECT documentdb_api.insert_one('db', 'powerMeterDataDouble', '{"powerMeterID": "1", "timeStamp": { "$date": { "$numberLong": "1589814930000" } }, "kilowatts": 1e308 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "powerMeterDataDouble", "pipeline": [ { "$setWindowFields": { "partitionBy": "$powerMeterID", "sortBy": { "timeStamp": 1 }, "output": { "powerMeterKilowattHours": { "$integral": { "input": "$kilowatts", "unit": "hour" }, "window": { "range": [ "unbounded", "current" ], "unit": "hour" } } } } }, { "$unset": "_id" } ]}');

-----------------------------------------------------------
-- $shift accumulator tests
-----------------------------------------------------------

---------------------
-- Negative Tests
---------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1 }, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}'); -- window not allowed with $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1 }, "window": {"range": [-1, 1]}}}}}]}'); -- range not allowed with $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1 }}}}}]}'); -- missing sortBy in $setWindowFields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "by": 1 }}}}}]}'); -- missing ouput field in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity" }}}}}]}'); -- missing by field in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1.1 }}}}}]}'); -- non-integer by field in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": {"t": 1} }}}}}]}'); -- non-integer by field in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": {"undefined": true} }}}}}]}'); -- non-integer by field in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1, "default": "$quantity" }}}}}]}'); -- non-constant default expression in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1, "default": ["$sasdf"] }}}}}]}'); -- non-constant default expression in $shift
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 2147483649}}}}}]}'); -- by value greater than int32 max value

---------------------
-- Valid tests
---------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": 1 }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": "$quantity", "by": -2 }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": ["$quantity", "$cost"], "by": 0 }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": ["$quantity", "$cost"], "by": 1, "default": "Not available" }}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity" : 1}, "output": {"shiftQuantity": { "$shift":  { "output": ["$quantity", "$cost"], "by": -2, "default": [0, 0] }}}}}]}');

-----------------------------------------------------------
-- $top accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$top": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');

-----------------------------------------------------------
-- $bottom accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$bottom": {"output": "$quantity", "sortBy": {"cost": -1}}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');


-----------------------------------------------------------
-- $topN accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$topN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');


-----------------------------------------------------------
-- $bottomN accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$bottomN": {"output": "$quantity", "sortBy": {"cost": -1}, "n": 3}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');    

/* Negative tests FOR $top(N)/$bottom(N) */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": [1]}}}}] }'); -- spec should be document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottomN": "hello"}}}}] }');  -- spec should be document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$top": {"output": "hello"}}}}} ] }'); -- missing sortBy
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottom": {"sortBy": {"quantity": 1}}}}}} ] }'); -- missing output field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "hello", "sortBy": {"quantity": 1}}}}}} ] }'); -- missing n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottomN": {"n": 2, "sortBy": {"quantity": 1}}}}}} ] }'); -- missing output
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottomN": {"output": "hello", "n": 2}}}}} ] }'); -- missing sortBy
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottomN": {"output": "$quantity", "n": 2, "sortBy": {"quantity": 1}, "fourth": true}}}}} ] }'); -- extra field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "$quantity", "n": -2, "sortBy": {"quantity": 1}}}}}} ] }'); -- negative n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "$quantity", "n": 2.2, "sortBy": {"quantity": 1}}}}}} ] }'); -- non-integer n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "$quantity", "n": "a", "sortBy": {"quantity": 1}}}}}} ] }'); -- non-integer n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "$quantity", "n": {"$undefined": true}, "sortBy": {"quantity": 1}}}}}} ] }'); -- undefined  n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$topN": {"output": "$quantity", "n": {"$numberDecimal": "Infinity"}, "sortBy": {"quantity": 1}}}}}} ] }'); -- undefined  n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$bottomN": {"output": "$quantity", "n": 2, "sortBy": 1}}}}} ] }'); -- sortBy is not an object

-----------------------------------------------------------
-- $stdDevPop & $stdDevSamp accumulator tests
-----------------------------------------------------------

-- empty collection
SELECT documentdb_api.insert_one('db','empty_stddev_col',' { "_id": 0, "num": {} }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_stddev_col", "pipeline":  [{"$setWindowFields": {"output": {"stdDevPop": { "$stdDevPop": "$num"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "empty_stddev_col", "pipeline":  [{"$setWindowFields": {"output": {"stdDevSamp": { "$stdDevSamp": "$num"}}}}]}');

-- collection with single document
SELECT documentdb_api.insert_one('db','single_stddev_col',' { "_id": 0, "num": 1 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_stddev_col", "pipeline":  [{"$setWindowFields": {"output": {"stdDevPop": { "$stdDevPop": "$num"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "single_stddev_col", "pipeline":  [{"$setWindowFields": {"output": {"stdDevSamp": { "$stdDevSamp": "$num"}}}}]}');

-- document with non-numeric values
SELECT documentdb_api.insert_one('db','non_numeric_stddev_col',' { "_id": 0, "num": "a"}');
SELECT documentdb_api.insert_one('db','non_numeric_stddev_col',' { "_id": 1, "num": "c"}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "non_numeric_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "non_numeric_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with mixed values
SELECT documentdb_api.insert_one('db','mixed_stddev_col',' { "_id": 0, "num": "a" }');
SELECT documentdb_api.insert_one('db','mixed_stddev_col',' { "_id": 1, "num": 0 }');
SELECT documentdb_api.insert_one('db','mixed_stddev_col',' { "_id": 2, "num": "b" }');
SELECT documentdb_api.insert_one('db','mixed_stddev_col',' { "_id": 3, "num": 1 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mixed_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "mixed_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with NAN values
SELECT documentdb_api.insert_one('db','nan_stddev_col',' { "_id": 0, "num": 2 }');
SELECT documentdb_api.insert_one('db','nan_stddev_col',' { "_id": 1, "num": 3 }');
SELECT documentdb_api.insert_one('db','nan_stddev_col',' { "_id": 2, "num": {"$numberDecimal": "NaN" } }');
SELECT documentdb_api.insert_one('db','nan_stddev_col',' { "_id": 3, "num": 4 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nan_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "nan_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with Infinity values
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 0, "type": "a", "num": 2 }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 1, "type": "a", "num": {"$numberDecimal": "Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 2, "type": "a", "num": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 3, "type": "a", "num": 4 }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 5, "type": "b", "num": 2 }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 6, "type": "b", "num": 3 }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 7, "type": "b", "num": {"$numberDecimal": "-Infinity" } }');
SELECT documentdb_api.insert_one('db','inf_stddev_col',' { "_id": 8, "type": "b", "num": {"$numberDecimal": "Infinity" } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "inf_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stddevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- document with Decimal128 values
SELECT documentdb_api.insert_one('db','decimal_stddev_col',' { "_id": 0, "num": 2 }');
SELECT documentdb_api.insert_one('db','decimal_stddev_col',' { "_id": 1, "num": 3 }');
SELECT documentdb_api.insert_one('db','decimal_stddev_col',' { "_id": 2, "num": {"$numberDecimal": "4.0" } }');
SELECT documentdb_api.insert_one('db','decimal_stddev_col',' { "_id": 3, "num": 5 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "decimal_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "decimal_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- number overflow
SELECT documentdb_api.insert_one('db','overflow_stddev_col',' { "_id": 0, "num": {"$numberDecimal": "100000004"} }');
SELECT documentdb_api.insert_one('db','overflow_stddev_col',' { "_id": 1, "num": {"$numberDecimal": "10000000007"} }');
SELECT documentdb_api.insert_one('db','overflow_stddev_col',' { "_id": 2, "num": {"$numberDecimal": "1000000000000013"} }');
SELECT documentdb_api.insert_one('db','overflow_stddev_col',' { "_id": 3, "num": {"$numberDecimal": "1000000000000000000"} }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "overflow_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "overflow_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- long values return double
SELECT documentdb_api.insert_one('db','long_stddev_col',' { "_id": 0, "x": 1, "num": {"$numberLong": "10000000004"} }');
SELECT documentdb_api.insert_one('db','long_stddev_col',' { "_id": 1, "x": 2, "num": {"$numberLong": "10000000007"} }');
SELECT documentdb_api.insert_one('db','long_stddev_col',' { "_id": 2, "x": 3, "num": {"$numberLong": "10000000013"} }');
SELECT documentdb_api.insert_one('db','long_stddev_col',' { "_id": 3, "x": 4, "num": {"$numberLong": "10000000016"} }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- large values return nan
SELECT documentdb_api.insert_one('db','large_stddev_col',' { "_id": 0, "num": {"$numberDecimal": "1E+310" } }');
SELECT documentdb_api.insert_one('db','large_stddev_col',' { "_id": 1, "num": {"$numberDecimal": "2E+310" } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "large_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": 1, "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "large_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": 1, "sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num"}}}}]}');

-- evaluate data in expression
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": {"$multiply": ["$x", "$num"]}, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": {"$multiply": ["$x", "$num"]}, "window": { "documents": ["unbounded", "current"]}}}}}]}');
-- array value
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": ["$num"], "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": ["$num"], "window": { "documents": ["unbounded", "current"]}}}}}]}');

-- negative cases
-- incorrect arguments won't throw error
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": 3, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": {"a": "$num"}, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": 3, "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "long_stddev_col", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": {"a": "$x"}, "window": { "documents": ["unbounded", "current"]}}}}}]}');


SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 0, "num": 2, "type": "a", "date": { "$date": { "$numberLong": "1718841600000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 1, "num": 3, "type": "a", "date": { "$date": { "$numberLong": "1718841605000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 2, "num": 4, "type": "a", "date": { "$date": { "$numberLong": "1718841610000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 3, "num": 5, "type": "a", "date": { "$date": { "$numberLong": "1718841615000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 4, "num": 6, "type": "a", "date": { "$date": { "$numberLong": "1718841620000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 5, "num": 7, "type": "a", "date": { "$date": { "$numberLong": "1718841630000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 6, "num": 8, "type": "b", "date": { "$date": { "$numberLong": "1718841640000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 7, "num": 9, "type": "b", "date": { "$date": { "$numberLong": "1718841680000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 8, "num": 10, "type": "b", "date": { "$date": { "$numberLong": "1718841700000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 9, "num": 11, "type": "b", "date": { "$date": { "$numberLong": "1718841800000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 10, "num": 12, "type": "b", "date": { "$date": { "$numberLong": "1718841900000" } } }');
SELECT documentdb_api.insert_one('db','window_stddev_col',' { "_id": 11, "num": 13, "type": "b", "date": { "$date": { "$numberLong": "1718842600000" } } }');

-- without window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num"}}}}]}');

-- document window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

-- range window
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": {"range": [-2, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": {"range": [-3, 3], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"range": [-2, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"range": [-3, 3], "unit": "minute"}}}}}]}');

-- unsharded collection
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": { "date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["unbounded", "current"]}}}}}]}');

-- shard collection
SELECT documentdb_api.shard_collection('db', 'window_stddev_col', '{ "type": "hashed" }', false);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": { "_id": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": "$type", "sortBy": {"_id": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": { "date": 1}, "output": {"stdDevPop": { "$stdDevPop": "$num", "window": { "documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "window_stddev_col", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$cond": [{ "$eq": [{ "$mod": ["$_id", 2] }, 0] }, "even", "odd"] }, "sortBy": {"date": 1}, "output": {"stdDevSamp": { "$stdDevSamp": "$num", "window": {"documents": ["unbounded", "current"]}}}}}]}');


-----------------------------------------------------------
-- $first accumulator tests
-----------------------------------------------------------

----------------------
-- No window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"firstDate": { "$first": {"output": "$date"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$first": {"output": "$quantity", "sortBy": {"cost": -1}}}}}}]}'); -- the sortBy inside $first is just treated as output expression

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$first": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$first": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$first": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$first": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------
-- Range window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$first": {"quantity": "$quantity", "cost": "$cost"}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$first": {"quantity": "$quantity", "cost": "$cost"}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');

-----------------------------------------------------------
-- $last accumulator tests
-----------------------------------------------------------

----------------------
-- No window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"firstDate": { "$last": {"output": "$date"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$last": {"output": "$quantity", "sortBy": {"cost": -1}}}}}}]}'); -- the sortBy inside $last is just treated as output expression

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$last": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$last": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$last": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$last": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------
-- Range window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$last": {"quantity": "$quantity", "cost": "$cost"}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$last": {"quantity": "$quantity", "cost": "$cost"}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');
    
-----------------------------------------------------------
-- $firstN accumulator tests
-----------------------------------------------------------

----------------------
-- No window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"firstDate": { "$firstN": {"input": "$date", "n": 2}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$firstN": {"input": "$quantity", "n": 3}}}}}]}');

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$firstN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$firstN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$firstN": {"input": "$quantity", "n": 4}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$firstN": {"input": "$quantity", "n": 5}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------
-- Range window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$firstN": {"input": {"quantity": "$quantity", "cost": "$cost"}, "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$firstN": {"input": {"quantity": "$quantity", "cost": "$cost"}, "n": 2}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');

-----------------------------------------------------------
-- $lastN accumulator tests
-----------------------------------------------------------

----------------------
-- No window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": { "$lastN": {"input": "$quantity", "n": 2}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$lastN": {"input": "$quantity", "n": 3}}}}}]}');

----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$lastN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", -1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$lastN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$lastN": {"input": "$quantity", "n": 4}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"cheapQuantity": { "$lastN": {"input": "$quantity", "n": 5}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------
-- Range window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$lastN": {"input": {"quantity": "$quantity", "cost": "$cost"}, "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$lastN": {"input": {"quantity": "$quantity", "cost": "$cost"}, "n": 2}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');

/* Negative tests for $firstN/$lastN */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": [1]}}}}] }'); -- spec should be document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$lastN": "hello"}}}}] }');  -- spec should be document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "hello"}}}}} ] }'); -- missing n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$lastN": {"n": 2}}}}} ] }'); -- missing input
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$lastN": {"input": "$quantity", "n": 2, "third": true}}}}} ] }'); -- extra field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "$quantity", "n": -2}}}}} ] }'); -- negative n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "$quantity", "n": 2.2}}}}} ] }'); -- non-integer n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "$quantity", "n": "a"}}}}} ] }'); -- non-integer n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "$quantity", "n": {"$undefined": true}}}}}} ] }'); -- undefined  n
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [ {"$setWindowFields": {"partitionBy": "$a", "output": {"lastDate": {"$firstN": {"input": "$quantity", "n": {"$numberDecimal": "Infinity"}}}}}} ] }'); -- undefined  n

/* Test $first, $last, $firstN, $lastN with $group stage as code path is common with $setWindowFields */
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$firstN": {"input": "$quantity", "n": 3 }}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$lastN": {"input": "$quantity", "n": 3}}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$lastN": {"input": ["$quantity", "$cost"], "n": 3}}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$first": {"input": ["$quantity", "$cost"], "n": 3}}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$first": "$quantity"}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$last": {"input": ["$quantity", "$cost"], "n": 3}}}}] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "setWindowFields", "pipeline": [{"$group": {"_id": "$a", "res": {"$last": "$quantity"}}}] }');

-----------------------------------------------------------
-- $maxN accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$maxN": {"input": "$quantity", "n": 3}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');


-----------------------------------------------------------
-- $minN accumulator tests
-----------------------------------------------------------

----------------------
-- Document window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"expensiveQuantity": { "$minN": {"input": "$quantity", "n": 3}, "window": {"range": [-20, 20], "unit": "second"}}}}}]}');


-----------------------------------------------------------
-- $min accumulator tests
-----------------------------------------------------------
----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$min": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": [0, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": [-10, 10]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["current", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["unbounded", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"totalQuantity": { "$min": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"totalQuantity": { "$min": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": [-5, 5], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$min": "$quantity", "window": {"range": [-5, 5], "unit": "day"}}}}}]}');
-- current in range doesn't mean current row its always the first peer which has same sort by value. This differs from MongoDB
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": ["current", "current"], "unit": "day"}}}}}]}');


-----------------------------------------------------------
-- $max accumulator tests
-----------------------------------------------------------
----------------------
-- Document window
----------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$max": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["unbounded", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["current", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": [0, 0]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": [-1, 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": [-10, 10]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["current", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["unbounded", 1]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"a": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["unbounded", -1]}}}}}]}');

----------------------------
-- Document window with sort
----------------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"_id": -1}, "output": {"totalQuantity": { "$max": "$quantity"}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"quantity": 1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": ["unbounded", "current"]}}}} }]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": {"date": -1}, "output": {"totalQuantity": { "$max": "$quantity", "window": {"documents": [-1, 1]}}}} }]}');

----------------------
-- Range window
----------------------

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": [-3, 3]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": ["unbounded", "current"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "quantity": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": ["current", "unbounded"]}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": [-5, 5], "unit": "second"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": [-5, 5], "unit": "minute"}}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": [-5, 5], "unit": "day"}}}}}]}');
-- current in range doesn't mean current row its always the first peer which has same sort by value. This differs from MongoDB
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"totalInGroup": { "$max": "$quantity", "window": {"range": ["current", "current"], "unit": "day"}}}}}]}');
