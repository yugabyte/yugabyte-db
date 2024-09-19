SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 466000;
SET helio_api.next_collection_id TO 4660;
SET helio_api.next_collection_index_id TO 4660;

----------------------------
-- Prepare data
----------------------------

SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);

SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "p2", "cost": 3, "date": { "$date": { "$numberLong": "1718841600011" } }, "quantity": 1 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } }, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "p2", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"} }, "quantity": 4  }', NULL);

SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 11, "a": "p3", "cost": 2, "date": { "$date": { "$numberLong": "1718841600021"} }, "quantity": 100  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 10, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841605022" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 9, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841606023" } } }', NULL);

----------------------------
-- positive case
----------------------------
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "$linearFill": "$quantity"}, "quantity2": { "$linearFill": "$quantity"}}}}]}');

-----------------------------------------------
-- positive case with different sort key value
-----------------------------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 1, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 501  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 2, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 9, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 10, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 510 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 1, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 501  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 2, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 9, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": -10, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 510 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 1, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "501" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 2, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 9, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 10, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 510 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 1, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "501" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 2, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 9, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 10, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 510 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 11, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": null  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 12, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 19, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "abc", "cost": 0, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 520 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

----------------------------
-- positive corner case
----------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": null  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": null }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 1  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": null }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 1  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600005"}}, "quantity": 1  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600006" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600007" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600008"}} , "quantity": null }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc1", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc4", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

---------------------------------------------
-- positive case with Infinity and -Infinity
---------------------------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": 500 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": { "$numberDecimal": "500" } }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": -Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": 500 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": -Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": -Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "a1", "b": "b1", "cost": 5, "date": { "$date": { "$numberLong": "1"}}, "quantity": -Infinity  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "a1", "b": "b2", "cost": 8, "date": { "$date": { "$numberLong": "2"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "a1", "b": "b1", "cost": 8, "date": { "$date": { "$numberLong": "3"}}, "quantity": -Infinity }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

--------------------------------------------
-- positive case to test manual window trim
--------------------------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": 500 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}, "quantity": 500 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600005"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600006"}}, "quantity": 600 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600007"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600008"}}, "quantity": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 9, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600009"}}, "quantity": 700 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

----------------------------------------------
-- positive case with different numeric types
----------------------------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600011"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": { "$numberInt": "501" }}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

-----------------------------------
-- positive corner case with sortBy
-----------------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "def", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "def", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
---------------------
-- negative case
---------------------

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);

-- without sortBy field
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

-- sortBy is not numeric or date
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "a": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "1", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "a": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

-- mixed numeric and date type in sortBy
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "1", "cost": 5, "date": 1, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "2", "cost": 8, "date": 2, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

-- sortBy with repeated value in one partition
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "test": 0, "val": 0}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "test": 1, "val": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "test": 9, "val": 9}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "test": 10, "val": 10}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "test": 10, "val": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 6, "test": 11, "val": 11}', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "test": 1 }, "output": {"val": { "$linearFill": "$val"}}}}]}');

-- fill non-numeric field
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": "503" }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600005"}} , "quantity": "503" }', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$linearFill": "$quantity"}}}}]}');

-- window fields existed in the input document
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$linearFill": "$quantity", "window":{ "documents": ["unbounded", "unbounded"]}}}}}]}');

------------------------
-- Infinity result test
------------------------
SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "test": 0, "val": { "$numberDecimal": "-9.999999999999999999999999999999999E+6144" }}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "test": 1, "val": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "test": 2, "val": { "$numberDecimal": "9.999999999999999999999999999999999E+6144" }}', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "test": 1 }, "output": {"val": { "$linearFill": "$val"}}}}]}');

SELECT helio_api.drop_collection('db','setWindowFields');
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 1, "test": 0, "val": { "$numberDecimal": "9.999999999999999999999999999999999E+6144" }}', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 2, "test": 1, "val": null }', NULL);
SELECT helio_api.insert_one('db','setWindowFields','{ "_id": 3, "test": 2, "val": { "$numberDecimal": "-9.999999999999999999999999999999999E+6144" }}', NULL);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "test": 1 }, "output": {"val": { "$linearFill": "$val"}}}}]}');
