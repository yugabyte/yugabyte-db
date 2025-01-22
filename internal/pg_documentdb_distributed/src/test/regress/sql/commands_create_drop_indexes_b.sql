SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 140000;
SET documentdb.next_collection_id TO 14000;
SET documentdb.next_collection_index_id TO 14000;

---- some other tests using createIndexes & dropIndexes ----

-- test 1

SELECT documentdb_api.create_collection('mydb', 'collection_2');

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "my_idx_1"}
     ]
   }'
   ,true
);

SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', 'my_idx_1');

CALL documentdb_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": "my_idx_1"
   }'
);

SELECT count(*)=0 AS index_does_not_exist
FROM documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', 'my_idx_1');

-- test 2

SELECT documentdb_api_internal.create_indexes_non_concurrently(
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

SELECT documentdb_indexes.index_name, documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', documentdb_indexes.index_name)
FROM (VALUES ('my_idx_1'), ('my_idx_2'), ('my_idx_3')) documentdb_indexes(index_name)
ORDER BY documentdb_indexes.index_name;

-- not the same index since this specifies wildcardProjection
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_2",
     "indexes": [
       {"key": {"$**": 1}, "name": "my_idx_4", "wildcardProjection": {"a": 0}}
     ]
   }',
   true
);

CALL documentdb_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": ["my_idx_1"]
   }'
);

CALL documentdb_api.drop_indexes(
  'mydb',
  '{
     "dropIndexes": "collection_2",
     "index": ["my_idx_2", "my_idx_3"]
   }'
);

SELECT count(*)=0 AS index_does_not_exist FROM (
    SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_2', documentdb_indexes.index_name)
    FROM (VALUES ('my_idx_1'), ('my_idx_2'), ('my_idx_3')) documentdb_indexes(index_name)
) q;

-- Cannot SELECT documentdb_api_internal.create_indexes_non_concurrently() in a xact block if collection
-- was created before and if we're really creating an index.
BEGIN;
SELECT documentdb_api_internal.create_indexes_non_concurrently(
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
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_200", "indexes": [{"key": {"$**": 1}, "name": "idx_1"}],
                                                                            "indexes": [{"key": {"$**": 1}, "name": "idx_2"}]}', true);

SELECT (index_spec).index_name FROM documentdb_api_catalog.collection_indexes
WHERE collection_id = (SELECT collection_id FROM documentdb_api_catalog.collections
                       WHERE collection_name = 'collection_200' AND database_name = 'db')
ORDER BY 1;

-- Can SELECT documentdb_api_internal.create_indexes_non_concurrently() in a xact block if we're not
-- creating any indexes, even if we're in a xact block.
BEGIN;
SELECT documentdb_api_internal.create_indexes_non_concurrently(
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

-- Can SELECT documentdb_api_internal.create_indexes_non_concurrently() in a xact block if collection
-- wasn't created before.

-- i) using create_collection()
BEGIN;
SELECT documentdb_api.create_collection('mydb', 'collection_new');
SELECT documentdb_api_internal.create_indexes_non_concurrently(
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
SELECT documentdb_api.insert_one('mydb','collection_new',' {"_id" : 1, "item" : "bread"}', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
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
SELECT documentdb_api_internal.create_indexes_non_concurrently(
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
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "new_idx_1"}
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_new",
     "indexes": [
       {"key": {"c.d": 1}, "name": "new_idx_2"}
     ]
   }'
);
ROLLBACK;
