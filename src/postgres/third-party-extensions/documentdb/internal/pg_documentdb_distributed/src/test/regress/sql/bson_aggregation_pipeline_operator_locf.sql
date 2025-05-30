SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 475000;
SET documentdb.next_collection_id TO 4750;
SET documentdb.next_collection_index_id TO 4750;
----------------------------
-- Prepare data
----------------------------
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);

SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "p2", "cost": 3, "date": { "$date": { "$numberLong": "1718841600011" } }, "quantity": 1 }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } }, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "p2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "p2", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"} }, "quantity": 4  }', NULL);

SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 10, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841605022" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 11, "a": "p3", "cost": 2, "date": { "$date": { "$numberLong": "1718841600021"} }, "quantity": 100  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 9, "a": "p3", "cost": 8, "date": { "$date": { "$numberLong": "1718841606023" } } }', NULL);

SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 12, "a": "p4", "cost": 2, "date": { "$date": { "$numberLong": "1718841600031"} }, "quantity": null  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 13, "a": "p4", "cost": 8, "date": { "$date": { "$numberLong": "1718841605032" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 14, "a": "p4", "cost": 8, "date": { "$date": { "$numberLong": "1718841606033" } } }', NULL);

----------------------------
-- positive case
----------------------------
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a","output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "_id": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": -1 },"output": {"quantity": { "$locf": "$quantity"}, "quantity2": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"output": {"quantity": { "$locf": "$quantity"}}}}]}');

----------------------------
-- positive corner case
----------------------------
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": null  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": null }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc1", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc2", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc3", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc4", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');

----------------------------------------------
-- positive case with different types
----------------------------------------------
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberLong": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600011"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600012" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 7, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600013" } } }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 8, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600014"}} , "quantity": { "$numberDouble": "503" } }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 },"output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": { "$numberDecimal": "500" }  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": { "$numberInt": "501" }}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 6, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600000"}}, "quantity": "string501"  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": "string501"  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": "string501"  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}, "quantity": null }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600004"}}, "quantity": 600}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600005"}}, "quantity": null }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": -1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

-- fill non-numeric field
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": "503" }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 5, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600005"}} , "quantity": "503" }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "date": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

-----------------------------------
-- positive corner case with sortBy
-----------------------------------
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "def", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "def", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

-- sortBy is not numeric or date
SELECT documentdb_api.drop_collection('db','setWindowFields');
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 1, "a": "abc", "cost": 5, "date": { "$date": { "$numberLong": "1718841600001"}}, "quantity": 500  }', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 2, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600002"}}, "quantity": null}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 4, "a": "abc", "cost": 8, "date": { "$date": { "$numberLong": "1718841600003"}}}', NULL);
SELECT documentdb_api.insert_one('db','setWindowFields','{ "_id": 3, "a": "abc", "cost": 4, "date": { "$date": { "$numberLong": "1718841600004"}} , "quantity": 503 }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "a": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

-- without sortBy field
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "output": {"quantity": { "$locf": "$quantity"}}}}]}');

-- sortBy with repeated value in one partition
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "$locf": "$quantity"}}}}]}');

---------------------
-- negative case
---------------------
-- window fields existed in the input document
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": "$a", "sortBy": { "cost": 1 }, "output": {"quantity": { "$locf": "$quantity", "window":{ "documents": ["unbounded", "unbounded"]}}}}}]}');
