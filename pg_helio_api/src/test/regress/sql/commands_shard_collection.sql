SET search_path TO helio_api, helio_api_catalog,helio_core;
SET helio_api.next_collection_id TO 5000;
SET helio_api.next_collection_index_id TO 5000;

-- before reshard
SELECT helio_api.insert_one('db','reshard','{"_id":"1", "value": { "$numberInt" : "11" }}');

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}';
ROLLBACK;

-- invalid inputs
SELECT helio_api.shard_collection('db','reshard', '{"value":1}');
SELECT helio_api.shard_collection('db','reshard', '{"value":"hash"}');

-- create two indexes before re-sharding
SELECT helio_api_internal.create_indexes_non_concurrently(
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

SELECT collection_id AS reshard_collection_id FROM helio_api_catalog.collections
WHERE collection_name = 'reshard' AND database_name = 'db' \gset

\d helio_data.documents_:reshard_collection_id

-- insert an invalid index metadata entry before re-sharding
INSERT INTO helio_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
VALUES (:reshard_collection_id, 2020, ('invalid_index', '{"c": 1}', null, null, null, null, 2, null), false);

-- shard based on value key
SELECT helio_api.shard_collection('db','reshard', '{"value":"hashed"}', false);

\d helio_data.documents_:reshard_collection_id

SELECT helio_api.collection_table('db','reshard') AS db_shard_data_table_name \gset

-- make plans (more) deterministic
VACUUM (ANALYZE) :db_shard_data_table_name;

SELECT * FROM helio_test_helpers.get_collection_indexes('db', 'reshard') ORDER BY index_id;

SELECT helio_api.insert_one('db','reshard','{"value":{"$numberLong" : "134311"}, "_id":"2" }');
SELECT helio_api.insert_one('db','reshard','{"_id":"3", "value": 11}');

-- documents without a shard key are allowed
SELECT helio_api.insert_one('db','reshard','{"_id":"4", "novalue": 0}');
SELECT count(*) FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$exists":false}}';

-- documents with an object shard key are allowed
SELECT helio_api.insert_one('db','reshard','{"_id":"5", "value": {"hello":"world"}}');
SELECT count(*) FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"hello":"world"}}';

-- documents with an array shard key are not allowed
SELECT helio_api.insert_one('db','reshard','{"_id":"6", "value": ["hello","world"]}');

-- documents with regex shard key are not allowed
SELECT helio_api.insert_one('db','reshard','{"_id":"6", "value":  {"$regularExpression":{"pattern":"foo","options":""}}}');

-- documents with double shard key are allowed
SELECT helio_api.insert_one('db','reshard','{"_id":"6", "value": 15.0}');

-- after reshard
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY document->'id';
ROLLBACK;

-- small longs have the same hash as ints
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberLong" : "134311"}}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberLong" : "134311"}}' ORDER BY document->'id';
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberInt" : "134311"}}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$numberInt" : "134311"}}' ORDER BY document->'id';
ROLLBACK;

-- should find doubles too when looking for int
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":15}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":15}' ORDER BY document->'id';
ROLLBACK;

-- reshard based on value and _id key
SELECT helio_api.shard_collection('db','reshard', '{"value":"hashed","_id":"hashed"}', true);

-- after reshard
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SET LOCAL helio_api.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"$and":[{"value":{"$eq":11}},{"_id":{"$eq":"1"}}]}';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"$and":[{"value":{"$eq":11}},{"_id":{"$eq":"1"}}]}';
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
SET LOCAL helio_api.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11,"_id":"1"}';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11,"_id":"1"}';
ROLLBACK;

SELECT helio_api_internal.get_shard_key_value('{"value":1,"_id":1}',1,'{"_id":"1","value":11}');
SELECT helio_api_internal.get_shard_key_value('{"value":1,"_id":1}',1,'{"value":11,"_id":"1"}');

-- different order of fields
SELECT helio_api_internal.get_shard_key_value('{"_id":1,"value":1}', 1,'{"_id":"1","value":11}');
SELECT helio_api_internal.get_shard_key_value('{"_id":1,"value":1}', 1,'{"value":11,"_id":"1"}');

-- should produce different hash values because type is taken into account
SELECT helio_api_internal.get_shard_key_value('{"a":1,"b":1}', 1,'{"_id":"1","a":1,"b":true}');
SELECT helio_api_internal.get_shard_key_value('{"a":1,"b":1}', 1,'{"_id":"1","a":true,"b":1}');

-- only 1 part of shard key specified, goes to multiple shards
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":11}' ORDER BY document->'id';
ROLLBACK;

-- no shard key filter specified
SELECT count(*) FROM helio_api.collection('db','reshard') WHERE true;
SELECT count(*) FROM helio_api.collection('db','reshard') WHERE false;
SELECT count(*) FROM helio_api.collection('db','reshard') WHERE document @@ '{"value":{"$exists":1}}';

-- reshard based on nested path
SELECT helio_api.shard_collection('db','reshard', '{"a.b":"hashed"}', true);

-- we should not allow arrays in the path even if the value is not an array
SELECT helio_api.insert_one('db','reshard','{"_id":"10", "a": [{"b":22}]}');

-- nested objects should be fine
SELECT helio_api.insert_one('db','reshard','{"_id":"10", "a": {"b":22}}');

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SET LOCAL seq_page_cost TO 9999999;
EXPLAIN (COSTS OFF)
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"a.b":22}' ORDER BY document->'id';
SELECT document FROM helio_api.collection('db','reshard') WHERE document @@ '{"a.b":22}' ORDER BY document->'id';
ROLLBACK;

-- try to shard a non-existent collection
SELECT helio_api.shard_collection('db','not_exists', '{"value":"hashed"}', false);

-- shard a collection that has no user-created indexes
SELECT helio_api.insert_one('db','shard_collection_no_indexes','{"_id":"1", "value": { "$numberInt" : "11" }}');

BEGIN;
  -- Shard it twice within a xact block to test whether we drop the temp table
  -- (v_saved_index_entries) before completing helio_api.shard_collection().
  SELECT helio_api.shard_collection('db','shard_collection_no_indexes', '{"value":"hashed"}', false);
  SELECT helio_api.shard_collection('db','shard_collection_no_indexes', '{"value":"hashed"}', false);
COMMIT;

-- shard creates a new collection.
BEGIN;
  SELECT helio_api.shard_collection('db', 'newCollectionToCreate', '{ "value": "hashed" }', false);

  SELECT database_name, collection_name FROM helio_api_catalog.collections WHERE collection_name = 'newCollectionToCreate' ORDER BY database_name, collection_name;
ROLLBACK;

-- shard collection with indexes with bson text as hex binary still works.
BEGIN;
set local helio_core.bsonUseEJson to false;
SELECT helio_api.insert_one('db','reshardwithindexes2','{"_id":"1", "value": 11, "otherValue": 15 }');

-- create some indexes
SELECT helio_api_internal.create_indexes_non_concurrently('db',
  '{
      "createIndexes": "reshardwithindexes2",
      "indexes": [
        {"key": { "value": 1 }, "name": "idx1"},
        {"key": { "otherValue": 1 }, "name": "idx2"}
      ]
  }',
  true
);

-- now shard the collection
SELECT helio_api.shard_collection('db', 'reshardwithindexes2', '{ "value": "hashed" }', false);

SELECT collection_id AS reshard_collection_id FROM helio_api_catalog.collections
WHERE collection_name = 'reshardwithindexes2' AND database_name = 'db' \gset

set local helio_core.bsonUseEJson to true;
SELECT index_spec from helio_api_catalog.collection_indexes WHERE collection_id = :reshard_collection_id order by index_id ASC;
SELECT database_name, collection_name, shard_key from helio_api_catalog.collections WHERE collection_id = :reshard_collection_id;
ROLLBACK;

-- create a new sharded collection
SELECT helio_api.create_collection('db', 'reshardoptions');

-- now shard it.
SELECT helio_api.shard_collection('db', 'reshardoptions', '{ "a.b": "hashed" }', false);

-- shard with the same key
SELECT helio_api.shard_collection('db', 'reshardoptions', '{ "a.b": "hashed" }');

-- shard with a new key with reshard:false (should fail)
SELECT helio_api.shard_collection('db', 'reshardoptions', '{ "c.d": "hashed" }', false);

-- key should now be { "a.b": "hashed" }
SELECT database_name, collection_name, shard_key FROM helio_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'reshardoptions';

-- shard with a new key with reshard:true (should work)
SELECT helio_api.shard_collection('db', 'reshardoptions', '{ "c.d": "hashed" }', true);

-- key should now be { "c.d": "hashed" }
SELECT database_name, collection_name, shard_key FROM helio_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'reshardoptions';

-- reshard on non existing collections or non-sharded collections should fail
SELECT helio_api.shard_collection('db', 'nonExistingCollection', '{ "a.b": "hashed" }', true);
SELECT helio_api.create_collection('db', 'nonShardedCollection');
SELECT helio_api.shard_collection('db', 'nonShardedCollection', '{ "a.b": "hashed" }', true);

