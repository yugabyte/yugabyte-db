SET search_path to helio_api_catalog;
SET citus.next_shard_id TO 11980000;
SET helio_api.next_collection_id TO 11980;
SET helio_api.next_collection_index_id TO 11980;

-- create new user with basic and insufficient permissions
CREATE ROLE user_2 WITH LOGIN PASSWORD 'pass';

GRANT ALL PRIVILEGES ON SCHEMA helio_api_catalog TO user_2;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA helio_api_catalog TO user_2;

GRANT SELECT ON ALL TABLES IN SCHEMA helio_data TO user_2;
GRANT USAGE ON SCHEMA helio_data TO user_2;
GRANT USAGE ON SCHEMA helio_api TO user_2;
GRANT USAGE ON SCHEMA helio_api_internal TO user_2;

-- create a collection
SELECT helio_api.create_collection('db', 'test_coll');

-- insert some data
SELECT helio_api.insert_one('db','test_coll','{"_id":"1", "a": { "$numberInt" : "11" }, "b": { "$numberInt" : "214748" }, "c": { "$numberInt" : "100" }}', NULL);
SELECT helio_api.insert_one('db','test_coll','{"_id":"2", "a": { "$numberInt" : "16" }, "b": { "$numberInt" : "214740" }, "c": { "$numberInt" : "-22" }}', NULL);
SELECT helio_api.insert_one('db','test_coll','{"_id":"3", "a": { "$numberInt" : "52" }, "b": { "$numberInt" : "121212" }, "c": { "$numberInt" : "101" }}', NULL);

-- switch to the new user (user_2)
SELECT current_user as original_user \gset
ALTER TABLE helio_data.documents_11980 OWNER TO user_2;
\c regression user_2

-- should fail index creation
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_coll", "indexes": [ { "key" : { "a": 1 }, "name": "index_a"}] }', true);

-- switch back to default user
\c regression :original_user
ALTER TABLE helio_data.documents_11980 OWNER TO helio_admin_role;

-- index creation should succeed
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test_coll", "indexes": [ { "key" : { "a": 1 }, "name": "index_a"}] }', true);

-- revoke all privileges and drop role
REVOKE ALL PRIVILEGES ON SCHEMA helio_api_catalog FROM user_2;
REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA helio_api_catalog FROM user_2;

REVOKE SELECT ON ALL TABLES IN SCHEMA helio_data FROM user_2;
REVOKE USAGE ON SCHEMA helio_data FROM user_2;
REVOKE USAGE ON SCHEMA helio_api FROM user_2;
REVOKE USAGE ON SCHEMA helio_api_internal FROM user_2;

DROP ROLE IF EXISTS user_2;
