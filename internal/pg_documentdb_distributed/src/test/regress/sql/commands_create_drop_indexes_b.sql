SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 140000;
SET helio_api.next_collection_id TO 14000;
SET helio_api.next_collection_index_id TO 14000;

---- some other tests using createIndexes & dropIndexes ----

-- test 1

SELECT helio_api.create_collection('mydb', 'collection_2');

SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "my_idx_1"}
     ]
   }'
   ,true
);

SELECT helio_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', 'my_idx_1');

CALL helio_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": "my_idx_1"
   }'
);

SELECT count(*)=0 AS index_does_not_exist
FROM helio_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', 'my_idx_1');

-- test 2

SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"a.b.$**": 1}, "name": "my_idx_1"},
       {"key": {"$**": 1}, "name": "my_idx_2"},
       {"key": {"c.d": 1}, "name": "my_idx_3"}
     ]
   }',
   true
);

SELECT helio_indexes.index_name, helio_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', helio_indexes.index_name)
FROM (VALUES ('my_idx_1'), ('my_idx_2'), ('my_idx_3')) helio_indexes(index_name)
ORDER BY helio_indexes.index_name;

-- not the same index since this specifies wildcardProjection
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"$**": 1}, "name": "my_idx_4", "wildcardProjection": {"a": 0}}
     ]
   }',
   true
);

CALL helio_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": ["my_idx_1"]
   }'
);

CALL helio_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": ["my_idx_2", "my_idx_3"]
   }'
);

SELECT count(*)=0 AS index_does_not_exist FROM (
    SELECT helio_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', helio_indexes.index_name)
    FROM (VALUES ('my_idx_1'), ('my_idx_2'), ('my_idx_3')) helio_indexes(index_name)
) q;

-- Cannot SELECT helio_api_internal.create_indexes_non_concurrently() in a xact block if collection
-- was created before and if we're really creating an index.
BEGIN;
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"},
       {"key": {"c.d": 1}, "name": "new_idx_2"}
     ]
   }',
   false
);
ROLLBACK;

-- would only create idx_2
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_200", "indexes": [{"key": {"$**": 1}, "name": "idx_1"}],
                                                                            "indexes": [{"key": {"$**": 1}, "name": "idx_2"}]}', true);

SELECT (index_spec).index_name FROM helio_api_catalog.collection_indexes
WHERE collection_id = (SELECT collection_id FROM helio_api_catalog.collections
                       WHERE collection_name = 'collection_200' AND database_name = 'db')
ORDER BY 1;

-- Can SELECT helio_api_internal.create_indexes_non_concurrently() in a xact block if we're not
-- creating any indexes, even if we're in a xact block.
BEGIN;
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"$**": 1}, "name": "my_idx_4", "wildcardProjection": {"a": 0}}
     ]
   }',
   true
);
ROLLBACK;

-- Can SELECT helio_api_internal.create_indexes_non_concurrently() in a xact block if collection
-- wasn't created before.

-- i) using create_collection()
BEGIN;
SELECT helio_api.create_collection('mydb', 'collection_new');
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"}
     ]
   }',
   true
);
ROLLBACK;

-- ii) using a command that implicitly creates the collection
--     if it doesn't exist, e.g.: insert()
BEGIN;
SELECT helio_api.insert_one('mydb','collection_new',' {"_id" : 1, "item" : "bread"}', NULL);
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"}
     ]
   }',
   true
);
ROLLBACK;

-- iii) implicitly creating via create_indexes()
BEGIN;
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"}
     ]
   }',
   true
);
ROLLBACK;

BEGIN;
SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"}
     ]
   }',
   true
);

SELECT helio_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"c.d": 1}, "name": "new_idx_2"}
     ]
   }'
);
ROLLBACK;
