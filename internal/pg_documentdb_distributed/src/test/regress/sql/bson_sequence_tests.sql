SET search_path TO helio_api,helio_api_internal,helio_core;
SET citus.next_shard_id TO 230000;
SET helio_api.next_collection_id TO 2300;
SET helio_api.next_collection_index_id TO 2300;

SELECT '{ "": [ { "a": 1 }, { "a": 2 } ] }'::bsonsequence;

SELECT bsonsequence_get_bson('{ "": [ { "a": 1 }, { "a": 2 } ] }'::bsonsequence);

SELECT bsonsequence_get_bson(bsonsequence_in(bsonsequence_out('{ "": [ { "a": 1 }, { "a": 2 } ] }'::bsonsequence)));

PREPARE q1(bytea) AS SELECT bsonsequence_get_bson($1);

EXECUTE q1('{ "": [ { "a": 1 }, { "a": 2 } ] }'::bsonsequence::bytea);

SELECT '{ "": [ { "a": 1 }, { "a": 2 } ] }'::bsonsequence::bytea::bsonsequence;

SELECT bsonsequence_get_bson('{ "a": 1 }'::bson::bsonsequence);

-- generate a long string and ensure we have the docs.
SELECT COUNT(*) FROM bsonsequence_get_bson(('{ "": [ ' || rtrim(REPEAT('{ "a": 1, "b": 2 },', 100), ',') || ' ] }')::bsonsequence);
