SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 12300;
SET documentdb.next_collection_index_id TO 12300;

-- Insert data
SELECT documentdb_api.insert_one('invmatchexp','airports','{ "_id": 1, "airport_id": 10165, "city": "Adak Island", "state": "AK", "name": "Adak", "rule": { "flight_type": "private"} }', NULL);
SELECT documentdb_api.insert_one('invmatchexp','airports','{ "_id": 3, "airport_id": 11308, "city": "Dothan", "state": "AL", "name": "Dothan Regional", "rule": { "$or": [ { "origin": "WA"}, {"flight_type": "private"}] } }', NULL);
SELECT documentdb_api.insert_one('invmatchexp','airports','{ "_id": 4, "airport_id": 11778, "city": "Fort Smith", "state": "AR", "name": "Fort Smith Regional", "rule": { "$in": [{"is_emergency": true}, {"is_vip": true}] }}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','airports','{ "_id": 6, "airport_id": 14689, "city": "Santa Barbara", "state": "CA", "name": "Santa Barbara Municipal", "rule": { "$or": [ {"$and": [{"flight_type": "private"}, {"origin": "CA"}]}, {"$or": [{"is_emergency": true}, {"is_vip": true}]} ] }}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','airports','{ "_id": 7, "airport_id": 13442, "city": "Everet", "state": "WA", "name": "Paine Field", "rule": { "tags": { "$all": ["private", "vip"]}}}', NULL);

SELECT documentdb_api.insert_one('invmatchexp','user_roles','{ "_id": 1, "user_id": 100, "roles": ["basic"]}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','user_roles','{ "_id": 2, "user_id": 101, "roles": ["basic", "sales"]}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','user_roles','{ "_id": 3, "user_id": 102, "roles": ["admin"]}', NULL);

SELECT documentdb_api.insert_one('invmatchexp','sales','{ "_id": 1, "order": 100, "paid": true, "total": 0, "rule": {"roles": {"$in": ["basic", "sales", "admin"]}}}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','sales','{ "_id": 2, "order": 102, "paid": true, "total": 1000, "rule": {"roles": {"$in": ["sales", "admin"]}}}', NULL);
SELECT documentdb_api.insert_one('invmatchexp','sales','{ "_id": 3, "order": 103, "paid": true, "total": 1000, "rule": {"roles": {"$in": ["admin"]}}}', NULL);

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document from bson_aggregation_pipeline('invmatchexp', '{ "aggregate": "sales", "pipeline": [ { "$inverseMatch": {"path": "rule", "from": "user_roles", "pipeline": [{"$match": {"user_id": { "$ne": 200 }}}, {"$limit": 1}]}} ], "cursor": {} }'); 
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document from bson_aggregation_pipeline('invmatchexp', '{ "aggregate": "sales", "pipeline": [ { "$inverseMatch": {"path": "rule", "from": "user_roles", "pipeline": [{"$match": {"user_id": { "$ne": 200 }}}]}} ], "cursor": {} }'); 
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document from bson_aggregation_pipeline('invmatchexp', '{ "aggregate": "sales", "pipeline": [ { "$inverseMatch": {"path": "rule", "input": {}}} ], "cursor": {} }'); 
