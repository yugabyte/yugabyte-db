SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 2400000;
SET documentdb.next_collection_id TO 24000;
SET documentdb.next_collection_index_id TO 24000;

-- before reshard
SELECT documentdb_api.insert_one('db','reshard','{"_id":"1", "value": { "$numberInt" : "11" }}');

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}';
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}';
ROLLBACK;

-- invalid inputs
SELECT documentdb_api.shard_collection('db','reshard', '{"value":1}');
SELECT documentdb_api.shard_collection('db','reshard', '{"value":"hash"}');

-- create two indexes before re-sharding
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "reshard",
     "indexes": [
       {"key": {"a.b.c.$**": 1}, "name": "idx_1"},
       {"key": {"z": 1}, "name": "idx_2"}
     ]
   }',
   true
);

SELECT collection_id AS reshard_collection_id FROM documentdb_api_catalog.collections
WHERE collection_name = 'reshard' AND database_name = 'db' \gset

\d documentdb_data.documents_:reshard_collection_id

-- insert an invalid index metadata entry before re-sharding
INSERT INTO documentdb_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
VALUES (:reshard_collection_id, 2020, ('invalid_index', '{"c": 1}', null, null, null, null, 2, null, null, null), false);

-- shard based on value key
SELECT documentdb_api.shard_collection('db','reshard', '{"value":"hashed"}', false);

\d documentdb_data.documents_:reshard_collection_id

SELECT FORMAT('documentdb_data.documents_%s', :reshard_collection_id) AS db_shard_data_table_name \gset

-- make plans (more) deterministic
VACUUM (ANALYZE) :db_shard_data_table_name;

SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'reshard') ORDER BY index_id;

SELECT documentdb_api.insert_one('db','reshard','{"value":{"$numberLong" : "134311"}, "_id":"2" }');
SELECT documentdb_api.insert_one('db','reshard','{"_id":"3", "value": 11}');

-- documents without a shard key are allowed
SELECT documentdb_api.insert_one('db','reshard','{"_id":"4", "novalue": 0}');
SELECT count(*) FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$exists":false}}';

-- documents with an object shard key are allowed
SELECT documentdb_api.insert_one('db','reshard','{"_id":"5", "value": {"hello":"world"}}');
SELECT count(*) FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"hello":"world"}}';

-- documents with an array shard key are not allowed
SELECT documentdb_api.insert_one('db','reshard','{"_id":"6", "value": ["hello","world"]}');

-- documents with regex shard key are not allowed
SELECT documentdb_api.insert_one('db','reshard','{"_id":"6", "value":  {"$regularExpression":{"pattern":"foo","options":""}}}');

-- documents with double shard key are allowed
SELECT documentdb_api.insert_one('db','reshard','{"_id":"6", "value": 15.0}');

-- after reshard
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY object_id;
ROLLBACK;

-- small longs have the same hash as ints
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberLong" : "134311"}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberLong" : "134311"}}' ORDER BY object_id;
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberInt" : "134311"}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberInt" : "134311"}}' ORDER BY object_id;
ROLLBACK;

-- should find doubles too when looking for int
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":15}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":15}' ORDER BY object_id;
ROLLBACK;

-- reshard based on value and _id key
SELECT documentdb_api.shard_collection('db','reshard', '{"value":"hashed","_id":"hashed"}', true);

-- after reshard
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"$and":[{"value":{"$eq":11}},{"_id":{"$eq":"1"}}]}';
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"$and":[{"value":{"$eq":11}},{"_id":{"$eq":"1"}}]}';
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11,"_id":"1"}';
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11,"_id":"1"}';
ROLLBACK;

-- should get same hash value
SELECT documentdb_api_internal.get_shard_key_value('{"value":1,"_id":1}',1,'{"_id":"1","value":11}');
SELECT documentdb_api_internal.get_shard_key_value('{"value":1,"_id":1}',1,'{"value":11,"_id":"1"}');

SELECT collection_id AS collection_id, shard_key::text AS shard_key FROM documentdb_api_catalog.collections
  WHERE collection_name = 'reshard' AND database_name = 'db' \gset

DO $$
DECLARE
  v_collection documentdb_api_catalog.collections;
  v_compute_shard_key bigint;
BEGIN
  SELECT * INTO v_collection FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'reshard';
  SELECT documentdb_api_internal.get_shard_key_value(v_collection.shard_key, v_collection.collection_id, '{"value":11,"_id":"1"}') INTO v_compute_shard_key;
  RAISE INFO 'Computed shard key: %', v_compute_shard_key;
END;
$$;

-- different order of fields
SELECT documentdb_api_internal.get_shard_key_value('{"_id":1,"value":1}', 1,'{"_id":"1","value":11}');
SELECT documentdb_api_internal.get_shard_key_value('{"_id":1,"value":1}', 1,'{"value":11,"_id":"1"}');

-- should produce different hash values because type is taken into account
SELECT documentdb_api_internal.get_shard_key_value('{"a":1,"b":1}', 1,'{"_id":"1","a":1,"b":true}');
SELECT documentdb_api_internal.get_shard_key_value('{"a":1,"b":1}', 1,'{"_id":"1","a":true,"b":1}');

-- only 1 part of shard key specified, goes to multiple shards
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY object_id;
ROLLBACK;

-- no shard key filter specified
SELECT count(*) FROM documentdb_api.collection('db','reshard') WHERE true;
SELECT count(*) FROM documentdb_api.collection('db','reshard') WHERE false;
SELECT count(*) FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"value":{"$exists":1}}';

-- reshard based on nested path
SELECT documentdb_api.shard_collection('db','reshard', '{"a.b":"hashed"}', true);

-- we should not allow arrays in the path even if the value is not an array
SELECT documentdb_api.insert_one('db','reshard','{"_id":"10", "a": [{"b":22}]}');

-- nested objects should be fine
SELECT documentdb_api.insert_one('db','reshard','{"_id":"10", "a": {"b":22}}');

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"a.b":22}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db','reshard') WHERE document @@ '{"a.b":22}' ORDER BY object_id;
ROLLBACK;

-- try to shard a non-existent collection
SELECT documentdb_api.shard_collection('db','not_exists', '{"value":"hashed"}', false);

-- shard a collection that has no user-created indexes
SELECT documentdb_api.insert_one('db','shard_collection_no_indexes','{"_id":"1", "value": { "$numberInt" : "11" }}');

BEGIN;
  -- Shard it twice within a xact block to test whether we drop the temp table
  -- (v_saved_index_entries) before completing documentdb_api.shard_collection().
  SELECT documentdb_api.shard_collection('db','shard_collection_no_indexes', '{"value":"hashed"}', false);
  SELECT documentdb_api.shard_collection('db','shard_collection_no_indexes', '{"value":"hashed"}', false);
COMMIT;

-- shard creates a new collection.
BEGIN;
  SELECT documentdb_api.shard_collection('db', 'newCollectionToCreate', '{ "value": "hashed" }', false);

  SELECT database_name, collection_name FROM documentdb_api_catalog.collections WHERE collection_name = 'newCollectionToCreate' ORDER BY database_name, collection_name;
ROLLBACK;

-- shard collection with indexes with bson text as hex binary still works.
BEGIN;
set local documentdb_core.bsonUseEJson to false;
SELECT documentdb_api.insert_one('db','reshardwithindexes2','{"_id":"1", "value": 11, "otherValue": 15 }');

-- create some indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('reshardwithindexes2', 'idx1', '{ "value": 1 }'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('reshardwithindexes2', 'idx2', '{ "otherValue": 1 }'), true);

-- now shard the collection
SELECT documentdb_api.shard_collection('db', 'reshardwithindexes2', '{ "value": "hashed" }', false);

SELECT collection_id AS reshard_collection_id FROM documentdb_api_catalog.collections
WHERE collection_name = 'reshardwithindexes2' AND database_name = 'db' \gset

set local documentdb_core.bsonUseEJson to true;
SELECT index_spec from documentdb_api_catalog.collection_indexes WHERE collection_id = :reshard_collection_id order by index_id ASC;
SELECT database_name, collection_name, shard_key from documentdb_api_catalog.collections WHERE collection_id = :reshard_collection_id;
ROLLBACK;

-- create a new sharded collection
SELECT documentdb_api.create_collection('db', 'reshardoptions');

-- now shard it.
SELECT documentdb_api.shard_collection('db', 'reshardoptions', '{ "a.b": "hashed" }', false);

-- shard with the same key
SELECT documentdb_api.shard_collection('db', 'reshardoptions', '{ "a.b": "hashed" }');

-- shard with a new key with reshard:false (should fail)
SELECT documentdb_api.shard_collection('db', 'reshardoptions', '{ "c.d": "hashed" }', false);

-- key should now be { "a.b": "hashed" }
SELECT database_name, collection_name, shard_key FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'reshardoptions';

-- shard with a new key with reshard:true (should work)
SELECT documentdb_api.shard_collection('db', 'reshardoptions', '{ "c.d": "hashed" }', true);

-- key should now be { "c.d": "hashed" }
SELECT database_name, collection_name, shard_key FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'reshardoptions';

-- reshard on non existing collections or non-sharded collections should fail
SELECT documentdb_api.shard_collection('db', 'nonExistingCollection', '{ "a.b": "hashed" }', true);
SELECT documentdb_api.create_collection('db', 'nonShardedCollection');
SELECT documentdb_api.shard_collection('db', 'nonShardedCollection', '{ "a.b": "hashed" }', true);
