SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_core;

SET documentdb.next_collection_id TO 7900;
SET documentdb.next_collection_index_id TO 7900;

SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 1, "airport_id": 10165, "city": "Adak Island", "state": "AK", "name": "Adak", "rule": { "flight_type": "private"} }', NULL);
SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 3, "airport_id": 11308, "city": "Dothan", "state": "AL", "name": "Dothan Regional", "rule": { "$or": [ { "origin": "WA"}, {"flight_type": "private"}] } }', NULL);
SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 4, "airport_id": 11778, "city": "Fort Smith", "state": "AR", "name": "Fort Smith Regional", "rule": { "$in": [{"is_emergency": true}, {"is_vip": true}] }}', NULL);
SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 6, "airport_id": 14689, "city": "Santa Barbara", "state": "CA", "name": "Santa Barbara Municipal", "rule": { "$or": [ {"$and": [{"flight_type": "private"}, {"origin": "CA"}]}, {"$or": [{"is_emergency": true}, {"is_vip": true}]} ] }}', NULL);
SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 7, "airport_id": 13442, "city": "Everet", "state": "WA", "name": "Paine Field", "rule": { "tags": { "$all": ["private", "vip"]}}}', NULL);
SELECT documentdb_api.insert_one('nested_db','airports','{ "_id": 8, "airport_id": 16123, "city": "Seattle", "state": "WA", "name": "Boeing Field", "rule": { "tags": { "$all": ["private", "vip"]}}}', NULL);


-- run a basic aggregation find
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}');

-- run a nested aggregation find
SELECT document FROM (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}')) basequery;

-- run 2 nested queries and join them
WITH q1 AS (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}')),
q2 AS (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "state": { "$gt": "A" }}}'))
SELECT q1.document, q2.document FROM q1, q2 WHERE q1.document->>'state' = q2.document->>'state';

-- with the GUC off nested fails, but basic works
set documentdb.allowNestedAggregationFunctionInQueries to off;
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}');
SELECT document FROM (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}')) basequery;
WITH q1 AS (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "airport_id": { "$gt": 12000, "$lt": 14000 }}}')),
q2 AS (SELECT document FROM documentdb_api_catalog.bson_aggregation_find('nested_db', '{ "find": "airports", "filter": { "state": { "$gt": "A" }}}'))
SELECT q1.document, q2.document FROM q1, q2 WHERE q1.document->>'state' = q2.document->>'state';