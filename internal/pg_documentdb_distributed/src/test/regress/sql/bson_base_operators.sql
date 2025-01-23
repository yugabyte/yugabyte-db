SET search_path TO helio_api,helio_api_internal,helio_core;
SET citus.next_shard_id TO 4700000;
SET helio_api.next_collection_id TO 4700;
SET helio_api.next_collection_index_id TO 4700;

SELECT '{"a":1,"b":"c"}'::bson->'a';
SELECT '{"a":1,"b":"c"}'::bson->'b';
SELECT '{"a":1,"b":["c"]}'::bson->'b';
SELECT '{"a":1,"b":{"c":3}}'::bson->'b';
SELECT '{"a":1,"b":"c"}'::bson->'c' IS NULL;
SELECT '{"a":1,"b":"c"}'::bson->NULL IS NULL;

SELECT bson_get_value('{"a":1,"b":"c"}', 'a');
SELECT bson_get_value('{"a":1,"b":"c"}', 'b');
SELECT bson_get_value('{"a":1,"b":["c"]}', 'b');
SELECT bson_get_value('{"a":1,"b":{"c":3}}', 'b');
SELECT bson_get_value('{"a":1,"b":"c"}', 'c') IS NULL;
SELECT bson_get_value('{"a":1,"b":"c"}', NULL) IS NULL;

SELECT * FROM bson_object_keys('{"a":1,"b":2}');
SELECT * FROM bson_object_keys('{"b":1,"a":2}');
SELECT * FROM bson_object_keys('{"b":1,"b":2}');
SELECT * FROM bson_object_keys(NULL);

SELECT '{ "a": { "$numberInt": "10" }, "b" : "c" }'::bson->>'a';
SELECT ('{ "a": { "$numberInt": "10" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberInt": "10" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberInt": "10" }, "b" : "c" }'::bson->>'a')::float;

SELECT '{ "a": { "$numberLong": "11" }, "b" : "c" }'::bson->>'a';
SELECT ('{ "a": { "$numberLong": "11" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberLong": "11" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberLong": "11" }, "b" : "c" }'::bson->>'a')::float;

SELECT '{ "a": { "$numberDouble": "11.12" }, "b" : "c" }'::bson->>'a';
SELECT ('{ "a": { "$numberDouble": "11" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberDouble": "11.12" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberDouble": "11" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberDouble": "11.12" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberDouble": "1.23e100" }, "b" : "c" }'::bson->>'a')::float;
SELECT ('{ "a": { "$numberDouble": "11.12" }, "b" : "c" }'::bson->>'a')::float;

SELECT '{ "a": { "$numberDecimal": "11.12" }, "b" : "c" }'::bson->>'a';
SELECT ('{ "a": { "$numberDecimal": "11" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberDecimal": "11.12" }, "b" : "c" }'::bson->>'a')::int;
SELECT ('{ "a": { "$numberDecimal": "11" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberDecimal": "11.12" }, "b" : "c" }'::bson->>'a')::int8;
SELECT ('{ "a": { "$numberDecimal": "1.23e100" }, "b" : "c" }'::bson->>'a')::float;
SELECT ('{ "a": { "$numberDecimal": "11.12" }, "b" : "c" }'::bson->>'a')::float;
SELECT ('{ "a": { "$numberDecimal": "1123123e2000" }, "b" : "c" }'::bson->>'a')::float;

SELECT '{ "a": true, "b" : "c" }'::bson->>'a';
SELECT '{ "a": false, "b" : "c" }'::bson->>'a';
SELECT ('{ "a": true, "b" : "c" }'::bson->>'a')::boolean;
SELECT ('{ "a": false, "b" : "c" }'::bson->>'a')::boolean;

-- other types other than string are returned as an object when used with ->>
SELECT '{ "a": {"$oid": "62e034e129274a635b24d895"}, "b" : "c" }'::bson->>'a';


WITH r1 AS (SELECT 1.0::float8 as dbl, 'text' AS txt, 3::int4 as int32, 44::int8 as int64, '{ "": [1, 2, 3]}'::bson as bsonValue, '{"a": 1, "b": 2}'::bson as bsonObj) SELECT row_get_bson(r) FROM r1 r;

SELECT '{"a":1,"b":"c"}'::bson::bytea::bson = '{"a":1,"b":"c"}'::bson;
