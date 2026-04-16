SET citus.next_shard_id TO 492000;
SET documentdb.next_collection_id TO 49200;
SET documentdb.next_collection_index_id TO 49200;

SET search_path TO documentdb_api_catalog;


DO $$
BEGIN
    FOR i IN 1..10000 LOOP
        PERFORM documentdb_api.insert_one('db', 'range_query', '{ "a": 1 }');
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api.insert_one('db', 'range_query', '{ "_id": 1, "a": 1 }', NULL);
SELECT documentdb_api.insert_one('db', 'range_query', '{ "_id": 2, "a": [1, 2, 3] }', NULL);
SELECT documentdb_api.insert_one('db', 'range_query', '{ "_id": 3, "a": [{ "b" : 1}, 2, [4, 5]] }', NULL);

BEGIN;
SET local client_min_messages TO ERROR;
set local citus.enable_local_execution to off; -- Simulate remote exection with a single nodes
Set local citus.log_remote_commands to on; -- Will print Citus rewrites of the queries
Set local citus.log_local_commands to on; -- Will print the local queries 
Set local log_statement to 'all'; -- Verbose logging
-- if there is no index we don't optimize the  a > 1 & a < 5 to  a <> {1, 5} (aka the range operator) 
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} }, "collation" : {"locale" : "en", "strength" : 1} }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} }, "collation" : {"locale" : "en", "strength" : 1}  }');
END;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_query",
     "indexes": [
       {"key": {"a": 1}, "name": "a_range"}
     ]
   }',
   true
);

BEGIN;
SET local client_min_messages TO ERROR;
set local citus.enable_local_execution to off; -- Simulate remote exection with a single nodes
Set local citus.log_remote_commands to on; -- Will print Citus rewrites of the queries
Set local citus.log_local_commands to on; -- Will print the local queries 
Set local log_statement to 'all'; -- Verbose logging
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');

-- when collation is present $push down of $range query is not done, and we use the unoptimized version (workitem=3423305)
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} }, "collation" : {"locale" : "en", "strength" : 1} }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} } ,"collation" : {"locale" : "en", "strength" : 1} }');
END;

BEGIN;
SET local client_min_messages TO ERROR;
set local citus.enable_local_execution to off; -- Simulate remote exection with a single nodes
Set local citus.log_remote_commands to on; -- Will print Citus rewrites of the queries
Set local citus.log_local_commands to on; -- Will print the local queries 
Set local log_statement to 'all'; -- Verbose logging
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$in": [ 5, 6, 7] }, "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "b": { "$in": [ 5, 6, 7] }, "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');
END;

-- Shard orders collection on item 
SELECT documentdb_api.shard_collection('db','range_query', '{"_id":"hashed"}', false);

BEGIN;
SET local client_min_messages TO ERROR;
set local citus.enable_local_execution to off; -- Simulate remote exection with a single nodes
Set local citus.log_remote_commands to on; -- Will print Citus rewrites of the queries
Set local citus.log_local_commands to on; -- Will print the local queries 
Set local log_statement to 'all'; -- Verbose logging

SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_query", "filter": { "a": { "$gt": 1 }, "a" : {"$lt" : 5} } }');
END;

SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [1, 2]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [3, 4]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [3, 1]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": {"test": 5}}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [{"test": 7}]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [true, false]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": 2}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": 3}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": 4}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [2]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [4]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [1, true]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [true, 1]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [1, 4]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [null]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": null}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": { "$minKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [{ "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [{ "$minKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [3, { "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": { "$maxKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [{ "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [{ "$maxKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": [3, { "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests', '{"val": []}', NULL);

SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1 }, "val" : {"$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1, "$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');


SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_js_tests",
     "indexes": [
       {"key": {"val": 1}, "name": "a_range"}
     ]
   }',
   true
);

SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1 }, "val" : {"$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');


BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1 }, "val" : {"$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1, "$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1 }, "val" : {"$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests", "filter": { "val": { "$gt": 1, "$lt" : 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
END;

-- tests For Minkey() and MaxKey() along with numbers
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": { "$maxKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": { "$minKey": 1 }}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [{ "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [{ "$minKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [3, { "$minKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [{ "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [{ "$maxKey": 1 }, 3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [3, { "$maxKey": 1 }]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": [3]}', NULL);
SELECT documentdb_api.insert_one('db', 'range_js_tests2', '{"val": 3}', NULL);

--      Runtime tests
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": {"$minKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": {"$maxKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lt": {"$minKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 1, "$lte": {"$maxKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": 100} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 1, "$lt": 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 3, "$lte": 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gt": 3, "$lt": 5} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 3, "$lt": 5} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');


--      Index tests
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_js_tests2",
     "indexes": [
       {"key": {"val": 1}, "name": "a_range"}
     ]
   }',
   true
);

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": {"$minKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": {"$maxKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lt": {"$minKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 1, "$lte": {"$maxKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": 100} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 1, "$lt": 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 3, "$lte": 3} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gt": 3, "$lt": 5} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": 3, "$lt": 5} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "range_js_tests2", "filter": { "val": { "$gte": {"$minKey" : 1}, "$lte": {"$maxKey" : 1}} }, "projection": { "_id": 0, "val": 1 }, "sort": { "val": 1 } }');
END;

SELECT documentdb_api.insert_one('db','daterangecoll', '{ "createdAt" : { "$date": { "$numberLong": "2657899731608" }} }', NULL);
SELECT documentdb_api.insert_one('db','daterangecoll', '{ "createdAt" : { "$date": { "$numberLong": "2657899831608" }} }', NULL);
SELECT documentdb_api.insert_one('db','daterangecoll', '{ "createdAt" : { "$date": { "$numberLong": "2657899931608" }} }', NULL);
SELECT documentdb_api.insert_one('db','daterangecoll', '{ "createdAt" : { "$date": { "$numberLong": "2657991031608" }} }', NULL);

SELECT document FROM bson_aggregation_find('db', '{ "find": "daterangecoll", "filter": { "createdAt": { "$gte": { "$date": { "$numberLong": "2657899731608" }}, "$lte": { "$date": { "$numberLong": "2657991031608" }} } }, "projection": { "_id": 0, "createdAt": 1 }, "sort": { "createdAt": 1 } }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "daterangecoll", "indexes": [{"key": {"createdAt": 1}, "name": "created_at_index"}]}', true);

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SELECT document FROM bson_aggregation_find('db', '{ "find": "daterangecoll", "filter": { "createdAt": { "$gte": { "$date": { "$numberLong": "2657899731608" }}, "$lte": { "$date": { "$numberLong": "2657991031608" }} } }, "projection": { "_id": 0, "createdAt": 1 }, "sort": { "createdAt": 1 } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "daterangecoll", "filter": { "createdAt": { "$gte": { "$date": { "$numberLong": "2657899731608" }}, "$lte": { "$date": { "$numberLong": "2657991031608" }} } }, "projection": { "_id": 0, "createdAt": 1 }, "sort": { "createdAt": 1 } }');
END;

-- do not convert $gte/$lte null
BEGIN;
SET LOCAL enable_seqscan TO OFF;
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "daterangecoll", "filter": { "createdAt": { "$gte": null, "$lte": { "$date": { "$numberLong": "2657991031608" }} } }, "projection": { "_id": 0, "createdAt": 1 }, "sort": { "createdAt": 1 } }');
EXPLAIN(costs off) SELECT document FROM bson_aggregation_find('db', '{ "find": "daterangecoll", "filter": { "createdAt": { "$gte": { "$date": { "$numberLong": "2657899731608" }}, "$lte": null } }, "projection": { "_id": 0, "createdAt": 1 }, "sort": { "createdAt": 1 } }');
END;


-- A local repro that had run into segfault

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_and_aggregation",
     "indexes": [
       {"key": {"idLinea": 1}, "name": "idLinea_1"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_and_aggregation",
     "indexes": [
       {"key": {"f_insercion": 1}, "name": "f_insercion_1"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "range_and_aggregation",
     "indexes": [
       {"key": {"idLinea": 1, "f_insercion": 1}, "name": "idLinea_1_f_insercion_1", "unique": true}     ]
   }',
   true
);


DO $$
BEGIN
    FOR i IN 1..1000 LOOP
        PERFORM documentdb_api.insert_one('db', 'range_and_aggregation', FORMAT('{ "f_insercion": { "$date" : { "$numberLong" : "%s" } }, "idLinea": 1, "n_18_velocidad": %s }',  i, i)::documentdb_core.bson);
    END LOOP;
END;
$$ LANGUAGE plpgsql;

BEGIN;
ALTER FUNCTION documentdb_api_catalog.bson_dollar_project COST 20000;
ANALYZE;

-- full query 500K
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "range_and_aggregation", "pipeline": [ { "$match" : { "$and" : [ { "f_insercion" : { "$gte" : { "$date" : { "$numberLong" : "0" } }}, "f_insercion" : { "$lt" : { "$date" : { "$numberLong" : "500000" } }} } ] }}, {"$project" : { "f_insercion" : 1, "n_18_velocidad": 1, "_id": 0, "date" : { "$dateToParts" : { "date" : "$f_insercion" } }} }, { "$group" : { "_id" : { "year" : "$date.year", "month" : "$date.month", "day" : "$date.day", "hour" : "$date.hour" }, "speed_avg" : { "$avg" : "$n_18_velocidad" } , "speed_max" : { "$max" : "$n_18_velocidad" } , "speed_min" : { "$min" : "$n_18_velocidad" }} }], "cursor": {} }');


-- full query 500K ( "idLinea": 1 filter ) 
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "range_and_aggregation", "pipeline": [ { "$match" : { "idLinea": 1, "$and" : [ { "f_insercion" : { "$gte" : { "$date" : { "$numberLong" : "0" } }}, "f_insercion" : { "$lt" : { "$date" : { "$numberLong" : "500000" } }} } ] }}, {"$project" : { "f_insercion" : 1, "n_18_velocidad": 1, "_id": 0, "date" : { "$dateToParts" : { "date" : "$f_insercion" } }} }, { "$group" : { "_id" : { "year" : "$date.year", "month" : "$date.month", "day" : "$date.day", "hour" : "$date.hour" }, "speed_avg" : { "$avg" : "$n_18_velocidad" } , "speed_max" : { "$max" : "$n_18_velocidad" } , "speed_min" : { "$min" : "$n_18_velocidad" }} }], "cursor": {} }');
ROLLBACK;