SET search_path TO helio_api,helio_core,helio_api_catalog;

SET helio_api.next_collection_id TO 3900;
SET helio_api.next_collection_index_id TO 3900;

-- Insert data
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 1, "airport_id": 10165, "city": "Adak Island", "state": "AK", "name": "Adak", "rule": { "flight_type": "private"} }', NULL);
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 3, "airport_id": 11308, "city": "Dothan", "state": "AL", "name": "Dothan Regional", "rule": { "$or": [ { "origin": "WA"}, {"flight_type": "private"}] } }', NULL);
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 4, "airport_id": 11778, "city": "Fort Smith", "state": "AR", "name": "Fort Smith Regional", "rule": { "$in": [{"is_emergency": true}, {"is_vip": true}] }}', NULL);
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 6, "airport_id": 14689, "city": "Santa Barbara", "state": "CA", "name": "Santa Barbara Municipal", "rule": { "$or": [ {"$and": [{"flight_type": "private"}, {"origin": "CA"}]}, {"$or": [{"is_emergency": true}, {"is_vip": true}]} ] }}', NULL);
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 7, "airport_id": 13442, "city": "Everet", "state": "WA", "name": "Paine Field", "rule": { "tags": { "$all": ["private", "vip"]}}}', NULL);

-- positive cases
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AK"}}, { "$inverseMatch": {"path": "rule", "input": {"flight_type": "public"}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AK"}}, { "$inverseMatch": {"path": "rule", "input": {"flight_type": "private"}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AL"}}, { "$inverseMatch": {"path": "rule", "input": {"flight_type": "public"}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AL"}}, { "$inverseMatch": {"path": "rule", "input": {"flight_type": "private"}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AR"}}, { "$inverseMatch": {"path": "rule", "input": {"is_emergency": true}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "CA"}}, { "$inverseMatch": {"path": "rule", "input": { "flight_type": "private", "origin": "CA" }}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "CA"}}, { "$inverseMatch": {"path": "rule", "input": { "flight_type": "public", "origin": "CA" }}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "CA"}}, { "$inverseMatch": {"path": "rule", "input": { "flight_type": "public", "origin": "CA", "is_vip": true }}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "AR"}}, { "$inverseMatch": {"path": "rule", "input": [{ "flight_type": "private"}, {"is_emergency": true}]}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{"$match": {"state": "WA"}}, { "$inverseMatch": {"path": "rule", "input": { "tags": ["private", "vip"]}}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{ "$inverseMatch": {"path": "rule", "input": { "origin": "WA" }}}]}');

-- Validate errors
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": {"pathRule": "rule", "input": {"flight_type": "private"} }}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": {"path": "rule", "inputValue": {"flight_type": "private"} }}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": {"path": "", "input": {"flight_type": "private"} }}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": {"path": "rule", "input": ["flight_type", "private"] }}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": {"path": "rule", "input": [{"flight_type": "private"}, ""] }}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [ { "$inverseMatch": [{"path": "rule", "input": [{"flight_type": "private"}, ""] }, {"path": "rule2", "input": {}}]}]}');

-- insert a document with an invalid query
SELECT helio_api.insert_one('invmatch','airports','{ "_id": 8, "airport_id": 13442, "city": "Everet", "state": "WA", "name": "Paine Field", "specialRule": { "tags": { "$allValues": ["private", "vip"]}}}', NULL);

-- any inverseMatch that queries that path should fail
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{ "$inverseMatch": {"path": "specialRule", "input": { "origin": "WA" }, "defaultResult": false}}]}');

-- if we query "rule" path without defaultResult in the spec it should fail for the Everet airport which doesn't define it
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{ "$inverseMatch": {"path": "rule", "input": { "origin": "WA" }}}]}');

-- with defaultResult true should return all documents that don't define the path and false shouldn't return them
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{ "$inverseMatch": {"path": "rule", "input": { "origin": "WA" }, "defaultResult": true}}]}');
SELECT document FROM bson_aggregation_pipeline('invmatch', '{ "aggregate": "airports", "pipeline": [{ "$inverseMatch": {"path": "rule", "input": { "origin": "WA" }, "defaultResult": false}}]}');


-- add tests with lookup for RBAC "like" scenarios
SELECT helio_api.insert_one('invmatch','user_roles','{ "_id": 1, "user_id": 100, "roles": ["basic"]}', NULL);
SELECT helio_api.insert_one('invmatch','user_roles','{ "_id": 2, "user_id": 101, "roles": ["basic", "sales"]}', NULL);
SELECT helio_api.insert_one('invmatch','user_roles','{ "_id": 3, "user_id": 102, "roles": ["admin"]}', NULL);

SELECT helio_api.insert_one('invmatch','sales','{ "_id": 1, "order": 100, "paid": true, "total": 0, "rule": {"roles": {"$in": ["basic", "sales", "admin"]}}}', NULL);
SELECT helio_api.insert_one('invmatch','sales','{ "_id": 2, "order": 102, "paid": true, "total": 1000, "rule": {"roles": {"$in": ["sales", "admin"]}}}', NULL);
SELECT helio_api.insert_one('invmatch','sales','{ "_id": 3, "order": 103, "paid": true, "total": 1000, "rule": {"roles": {"$in": ["admin"]}}}', NULL);

SELECT document from bson_aggregation_pipeline('invmatch', '{ "aggregate": "sales", "pipeline": [ { "$lookup": { "from": "user_roles", "pipeline": [ { "$match": {"user_id": 100} } ], "as": "roles" }}, { "$inverseMatch": {"path": "rule", "input": "$roles"}}, {"$project": {"roles": 0, "rule": 0}} ], "cursor": {} }');
SELECT document from bson_aggregation_pipeline('invmatch', '{ "aggregate": "sales", "pipeline": [ { "$lookup": { "from": "user_roles", "pipeline": [ { "$match": {"user_id": 101} } ], "as": "roles" }}, { "$inverseMatch": {"path": "rule", "input": "$roles"}}, {"$project": {"roles": 0, "rule": 0}} ], "cursor": {} }');
SELECT document from bson_aggregation_pipeline('invmatch', '{ "aggregate": "sales", "pipeline": [ { "$lookup": { "from": "user_roles", "pipeline": [ { "$match": {"user_id": 102} } ], "as": "roles" }}, { "$inverseMatch": {"path": "rule", "input": "$roles"}}, {"$project": {"roles": 0, "rule": 0}} ], "cursor": {} }');