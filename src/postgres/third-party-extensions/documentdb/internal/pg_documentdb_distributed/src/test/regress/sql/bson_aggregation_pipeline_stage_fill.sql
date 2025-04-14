SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;
SET citus.next_shard_id TO 468000;
SET documentdb.next_collection_id TO 4680;
SET documentdb.next_collection_index_id TO 4680;

----------------------------
-- Prepare data
----------------------------

SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);

SELECT documentdb_api.insert_one('db','filltest','{ "_id": 5, "a": "p2", "cost": 3, "date": { "$date": { "$numberLong": "1718841600011" } }, "quantity": 1 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 6, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } }, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 7, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 8, "a": "p2", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"} }, "quantity": 4  }', NULL);

SELECT documentdb_api.insert_one('db','filltest','{ "_id": 11, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600021"} }, "quantity": 100  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 10, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841605022" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 9, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841606023" } } }', NULL);

----------------------------
-- positive case
----------------------------
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "_id": 1 },"output": {"quantity": { "method": "linear"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "method": "linear"}}}}]}');

SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "locf"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "date": 1 },"output": {"quantity": { "method": "locf"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "_id": 1 },"output": {"quantity": { "method": "locf"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "method": "locf"}}}}]}');

SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "value": 123}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "date": 1 },"output": {"quantity": { "value": 567}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "_id": 1 },"output": {"quantity": { "value": "test"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "value": {"$numberDecimal": "123"}}}}}]}');

SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionByFields": ["a"], "sortBy": { "date": 1 },"output": {"quantity": { "value": 123}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionByFields": ["a"], "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionByFields": ["a", "cost"], "sortBy": { "date": 1 },"output": {"quantity": { "method": "locf"}}}}]}');

----------------------------
-- positive corner case
----------------------------
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": null  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": null }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "locf"}}}}]}');

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc1", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc4", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');

--------------------------------------------
-- positive case to test manual window trim
--------------------------------------------
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": 500 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}, "quantity": 500 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}}, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600005"}}, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600006"}}, "quantity": 600 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600007"}}, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600008"}}, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 9, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600009"}}, "quantity": 700 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionByFields": ["a"], "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');

----------------------------------------------
-- positive case with different numeric types
----------------------------------------------
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600011"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "method": "linear"}}}}]}');

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": { "$numberInt": "501" }}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-----------------------------------
-- positive corner case with sortBy
-----------------------------------
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "def", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "def", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

---------------------
-- negative case
---------------------

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);

-- without sortBy field
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "output": {"quantity": { "method": "linear"}}}}]}');

-- sortBy is not numeric or date
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "a": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-- sortBy with repeated value in one partition
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-- partionBy and partitionByFields both are present
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "partitionByFields": ["a"], "sortBy": { "date": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-- partitionByFields element starts with $
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionByFields": ["$a"], "sortBy": { "date": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-- output field is not present in the document
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 }}}]}');

-- fill non-numeric field
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": "503" }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 5, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600005"}} , "quantity": "503" }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "method": "linear"}}}}]}');

-- fill with missing value
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": 1, "c": 5 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": 2, "c": null  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": 3  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4 }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"output": {"c": { "value": "$a"}}}}]}');

-- fill with mixed types
SELECT documentdb_api.drop_collection('db','filltest');
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 1, "a": 1, "b": 1, "c": 5 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 2, "a": 2, "b": null, "c": null  }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 3, "a": 3, "b": 3 }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 4, "a": 4, "b": null }', NULL);
SELECT documentdb_api.insert_one('db','filltest','{ "_id": 5, "a": 5, "b": 5, "c": null }', NULL);
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "b": { "method": "linear" } ,"c": { "value": "$a" }}}}]}');
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "b": { "method": "locf" } ,"c": { "value": "$a" }}}}]}');

-- fill with multi methods
SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "b": { "method": "locf", "method": "linear" }}}}]}');


-- explain plan validation
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "b": { "method": "linear" }}}}]}');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "b": { "value": 1 }}}}]}');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate": "filltest", "pipeline":  [{"$fill": {"sortBy": {"a" : 1}, "output": { "c": { "method": "linear" }, "b": { "value": 1 }}}}]}');
