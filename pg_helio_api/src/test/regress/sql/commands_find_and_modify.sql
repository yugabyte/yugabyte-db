SET helio_api.next_collection_id TO 3000;
SET helio_api.next_collection_index_id TO 3000;

-- null db name
SELECT helio_api.find_and_modify(NULL, '{}');

-- null message
SELECT helio_api.find_and_modify('db', NULL);

-- missing params
SELECT helio_api.find_and_modify('fam', '{}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "remove_or_update"}');

-- no such collection, upsert=false
--  i) remove=true
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "dne", "query": {"a": 1000}, "remove": 0.1, "sort": {"b": -1}}');
--  ii) remove=false
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "dne", "query": {"a": 1}, "update": {"_id": 1, "b": 1}, "upsert": false}');

-- no such collection, upsert=true
--  i) query is given
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "create_on_fam_1", "query": {"a": 1}, "update": {"_id": 1, "b": 1}, "upsert": 1.1}');
SELECT document FROM helio_api.collection('fam', 'create_on_fam_1') ORDER BY document;
--  ii) query is not given, and the upserted document is requested
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "create_on_fam_2", "update": {"_id": 1, "b": 1}, "upsert": true, "new": -1}');
SELECT document FROM helio_api.collection('fam', 'create_on_fam_2') ORDER BY document;
--  iii) enable_create_collection_on_insert is disabled
BEGIN;
  SET LOCAL helio_api.enable_create_collection_on_insert TO OFF;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "create_on_fam_3", "update": {"_id": 1, "b": 1}, "upsert": true}');
ROLLBACK;

-- test conflicting options
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "opts_conflict", "remove": true, "update": {"b": 1}}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "opts_conflict", "remove": true, "upsert": true}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "opts_conflict", "remove": true, "new": true}');

-- field type validations
SELECT helio_api.find_and_modify('fam', '{"findAndModify": []}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "query": 1}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "sort": "text"}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "remove": {}}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "update": 1}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "new": []}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "fields": "text"}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "invalid_type", "upsert": []}');

-- hard errors for unsupported options
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "not_supported", "arrayFilters": 1}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "not_supported", "hint": 1}');

-- unknown option
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "unknown_option", "unknown_option": 1}');

SELECT 1 FROM helio_api.insert_one('fam', 'collection', '{"a":5,"b":7}');
SELECT 1 FROM helio_api.insert_one('fam', 'collection', '{"a":5,"b":5}');
SELECT 1 FROM helio_api.insert_one('fam', 'collection', '{"a":5,"b":6}');

-- Disallow writes to system.views
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "system.views", "query": null, "remove": 0.0, "sort": {"b": -1}, "update": {"a": 10}, "fields": {"_id": 0}}');

BEGIN;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": null, "remove": 0.0, "sort": {"b": -1}, "update": {"a": 10}, "fields": {"_id": 0}}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 5}, "sort": {"b": 1}, "update": 1, "update": {"a": 20}, "fields": {"_id": 0}}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "sort": {"b": 1}, "update": {"a": 1}, "fields": {"_id": 0, "b": 0}, "upsert": 0, "new": false}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "sort": "", "sort": {"b": 1}, "update": {"a": 1}, "fields": {"_id": 0, "b": 0}, "upsert": false, "new": true}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "sort": {"b": 1}, "update": {"_id": 40, "a": 30}, "fields": {"b": 1, "_id": 0}, "upsert": true}');

  -- using update operators / aggregation pipeline --

  -- multiple $inc, so only takes the last one into the account
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": { "$gte": 15 } }, "sort": {"a": 1}, "update": {"$set": {"z": 5}, "$inc": {"z": 5}, "$inc": {"a": 10}}, "upsert": false, "new": true, "fields": {"_id": 0}}');

  -- multiple $set/$inc but provided via a single document, so applies all
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 1000 }, "update": {"$set": {"_id": 1000, "p": 10, "r": 20}, "$inc": {"s": 30, "t": 40}}, "upsert": true, "new": true, "fields": {"_id": 0}}');

  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"z": { "$exists": false } }, "sort": {"a": 1}, "update": [{"$set": {"a": -10}}, {"$addFields": {"z": 7}}], "upsert": false, "new": true, "fields": {"_id": 0}}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 2000 }, "update": [ {"$set": {"p": 40, "_id": 2000, "r": 50}}, {"$unset": "p"}, {"$set": {"r": 70}}], "new": true, "fields": {"_id": 0}, "upsert": 1}');
ROLLBACK;

BEGIN;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "sort": {"b": 1}, "update": {"_id": 40, "a": [ 30 ]}, "fields": {"b": 1, "_id": 0}, "upsert": true}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "sort": {"b": 1}, "update": { "$set": { "a.$[a]": 10 }}, "fields": {"b": 1, "_id": 0}, "upsert": true, "arrayFilters": [ { "a": 30 } ]}');
ROLLBACK;

BEGIN;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": { "$gte": 15 } }, "sort": {}, "update": {"$set": {"z": 5}, "$inc": {"z": 5}, "$inc": {"a": 10}}, "upsert": false, "new": true, "fields": {}}');
ROLLBACK;

BEGIN;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": null, "remove": true, "sort": {"b": -1}, "fields": {"_id": 0, "a": 0}}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 5}, "remove": true, "sort": {"b": 1}, "fields": {"_id": 0, "b": 1}, "upsert": null}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "remove": true, "sort": {"b": 1}, "fields": {"_id": 0}}');
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"a": 100}, "remove": true, "sort": {}, "fields": {}}');
ROLLBACK;

-- test a sharded collection
SELECT helio_api.create_collection('fam','sharded_collection');
SELECT helio_api.shard_collection('fam','sharded_collection', '{"a":"hashed"}', false);

SELECT 1 FROM helio_api.insert_one('fam', 'sharded_collection', '{"a": 10,"b":7}');
SELECT 1 FROM helio_api.insert_one('fam', 'sharded_collection', '{"a":20,"b":5}');
SELECT 1 FROM helio_api.insert_one('fam', 'sharded_collection', '{"a":30,"b":6}');
SELECT 1 FROM helio_api.insert_one('fam', 'sharded_collection', '{"b":8}');
SELECT 1 FROM helio_api.insert_one('fam', 'sharded_collection', '{"b":9,"a": null}');

-- update the shard key
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": 10}, "update": {"$set": {"a": 1000}}, "fields": {"_id": 0}, "new": true}');
-- update a field other than the shard key
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": 20}, "update": {"$set": {"b": -1}}, "fields": {"_id": 0}, "new": true}');
-- test upsert
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": -1}, "update": {"$set": {"b": -2, "_id": 100}}, "new": true, "upsert": true}');
-- test "null" shard key: i) shard key is really equal to null
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": null}, "update": {"$set": {"b": -3}}, "fields": {"_id": 0}, "sort": {"b": -1}, "new": true}');
-- test "null" shard key: ii) shard key is not set
-- should update the document having {"b": 8} even if it doesn't specify "a" field at all
BEGIN;
  SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": null}, "update": {"$set": {"b": -4}}, "fields": {"_id": 0}, "sort": {"b": -1}, "new": false}');
ROLLBACK;
-- test "null" shard key: iii) shard key is not set
-- should update the document having {"b": -3}
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": null}, "update": {"b": -4}, "fields": {"_id": 0}, "sort": {"b": 1}, "new": false}');

-- missing shard key
--SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"a": { "$gte": 15 } }, "update": {"$set": {"z": 5}}}');
--SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": null, "update": {"a": 10}}');
--SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"b": -2}, "remove": true}');

SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"b": -2, "a": -1}, "remove": true}');
-- should match the document having {"b": -4} even if it doesn't specify "a" field at all
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "sharded_collection", "query": {"b": -4, "a": null}, "remove": true, "fields": {"_id": 0}}');

-- show that we validate "update" document even if collection doesn't exist or if we can't match any documents
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "dne", "query": {"a": 1}, "update": { "$set": { "a": 1 }, "$unset": {"a": 1 } } }');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "dne", "query": {"$a": 1}, "update": { "$set": { "a": 1 } } }');
SELECT helio_api.create_collection('fam', 'no_match');
\set VERBOSITY TERSE
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "no_match", "update": { "$set": { "a": 1 }, "$unset": {"a": 1 } }}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "no_match", "query": {"$a": 1}, "update": { "$set": { "a": 1 } } }');
\set VERBOSITY DEFAULT

-- test retryable update
SELECT 1 FROM helio_api.insert_one('fam', 'retryable_update', '{"_id": 1, "a": 1, "b": 1}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": false}', 'xact-1');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": true}', 'xact-1');
SELECT document FROM helio_api.collection('fam', 'retryable_update') ORDER BY document;

-- third call is considered a new try
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": true, "fields": {"a": 0}}', 'xact-1');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": true, "fields": {"_id": 0}}', 'xact-1');
SELECT document FROM helio_api.collection('fam', 'retryable_update') ORDER BY document;

SELECT helio_api.shard_collection('fam','retryable_update', '{"a":"hashed"}', false);

-- test with upsert
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"_id": 2, "a": 100}, "update": {"$inc": {"b": 1}}, "new": true, "upsert": true, "fields": {"a": 0}}', 'xact-1');
-- Note that specifying different values for "new"/"fields" fields doesn't
-- have any effect on the response message.
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"_id": 2, "a": 100}, "update": {"$inc": {"b": 1}}, "new": false, "upsert": true, "fields": {"b": 0}}', 'xact-1');
SELECT document FROM helio_api.collection('fam', 'retryable_update') ORDER BY document;

-- test with upsert, collection gets created automatically
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update_dne", "query": {"_id": 2, "a": 100}, "update": {"$inc": {"b": 1}}, "new": false, "upsert": true}', 'xact-2');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update_dne", "query": {"_id": 2, "a": 100}, "update": {"$inc": {"b": 1}}, "new": true, "upsert": true}', 'xact-2');
SELECT document FROM helio_api.collection('fam', 'retryable_update_dne') ORDER BY document;

-- test retryable delete
SELECT 1 FROM helio_api.insert_one('fam', 'retryable_delete', '{"_id": 1, "a": 1, "b": 1}');
SELECT 1 FROM helio_api.insert_one('fam', 'retryable_delete', '{"_id": 2, "a": 1, "b": 1}');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 1}, "remove": true}', 'xact-11');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 1}, "remove": true}', 'xact-11');
SELECT document FROM helio_api.collection('fam', 'retryable_delete') ORDER BY document;

-- third call is considered a new try
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 1}, "remove": true, "fields": {"b": 0}}', 'xact-11');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 1}, "remove": true, "fields": {"a": 0}}', 'xact-11');
SELECT document FROM helio_api.collection('fam', 'retryable_delete') ORDER BY document;

SELECT 1 FROM helio_api.insert_one('fam', 'retryable_delete_sharded', '{"_id": 1, "a": 1, "b": 1}');
SELECT 1 FROM helio_api.insert_one('fam', 'retryable_delete_sharded', '{"_id": 2, "a": 1, "b": 1}');

SELECT helio_api.shard_collection('fam','retryable_delete_sharded', '{"a":"hashed"}', false);

SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_sharded", "query": {"a": 1}, "remove": true, "fields": {"b": 0}}', 'xact-14');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_sharded", "query": {"a": 1}, "remove": true, "fields": {"a": 0}}', 'xact-14');
SELECT document FROM helio_api.collection('fam', 'retryable_delete_sharded') ORDER BY document;

-- third call is considered a new try
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_sharded", "query": {"a": 1}, "remove": true}', 'xact-14');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_sharded", "query": {"a": 1}, "remove": true}', 'xact-14');
SELECT document FROM helio_api.collection('fam', 'retryable_delete_sharded') ORDER BY document;

-- test with a query that doesn't match any documents
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 100}, "remove": true}', 'xact-11');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete", "query": {"a": 100}, "remove": true}', 'xact-11');

-- test with a collection that doesn't exist
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_dne", "query": {"a": 100}, "remove": true}', 'xact-13');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_delete_dne", "query": {"a": 100}, "remove": true}', 'xact-13');

SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": true, "fields": {"a": 0}}', 'xact-20');
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "retryable_update", "query": {"a": 1}, "update": {"$inc": {"b": 1}}, "new": true, "fields": {"a": 0}}', 'xact-20');
SELECT document FROM helio_api.collection('fam', 'retryable_update') ORDER BY document;

-- unknown operator expressions in fields argument
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"_id": 1}, "update": {"$inc": {"b": 1}}, "new": true, "upsert": true, "fields": {"foo": {"$pop": ["bar"]}}}');

-- test with operator expression in fields argument
SELECT helio_api.find_and_modify('fam', '{"findAndModify": "collection", "query": {"_id": 1}, "update": {"$inc": {"b": 1}}, "new": true, "upsert": true, "fields": {"foo": {"$pow": [1, 2]}}}');
