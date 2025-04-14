SET citus.next_shard_id TO 120000;
SET documentdb.next_collection_id TO 12000;
SET documentdb.next_collection_index_id TO 12000;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('compound_index_test', 'compound_index', '{"a.b": 1, "c.d": 1}'), true);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','compound_index_test');

BEGIN;
  set local enable_seqscan TO off;
  EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'compound_index_test') WHERE document @@ '{ "a.b": { "$gte" : 1 }}';
ROLLBACK;

-- supported "partialFilterExpression" operators --

-- note that inner $and is not an operator
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "$and": [
             { "a": { "b": { "x": {"$and": [ {"$eq": 1} ] }, "y": [1]} , "c": 3} },
             { "b": {"$gte": 10} }
           ]
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_1');

-- note that it's not the $regex operator
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"f.$**": 1}, "name": "my_idx_2",
         "partialFilterExpression":
         {
           "item": {"a": {"$regex": "^p.*", "$options": "si"}}
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_2');

-- $exists: true is supported
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"r.$**": 1}, "name": "my_idx_3",
         "partialFilterExpression":
         {
           "$and": [
             {"b": 55},
             {"a": {"$exists": true}},
             {"c": {"$exists": 1}},
             {"d": {"$exists": -1}}
            ]
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_3');

-- While Mongo throws an error for the following:
--
-- "partialFilterExpression" {
--   "$and": [{"p": 1}, {"q": 2}],
--   "b": [{"z": 1}, {"t": 2}]
-- }
--
-- , doesn't throw an error for below by ignoring the first $and expression.
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"p.$**": 1}, "name": "my_idx_4",
         "partialFilterExpression":
         {
           "$and": [{"p": 1}, {"q": 2}],
           "$and": [{"z": 1}, {"t": 2}]
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_4');

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb_4',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "my_idx_5",
         "partialFilterExpression":
         {
           "a": [
             {"a": "b"}, "c", 1
           ],
           "b": {
             "c": {"d": [1,2,3]},
             "e": {"f": 0}
           },
           "c": {"$exists": 1}
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb_4', 'collection_10', 'my_idx_5');


-- force using my_idx_5 when possible
SELECT documentdb_distributed_test_helpers.drop_primary_key('mydb_4', 'collection_10');
SELECT SUM(1) FROM documentdb_api.insert_one('mydb_4','collection_10', '{"c":"foo"}'), generate_series(1, 10);

BEGIN;
  set local enable_seqscan TO OFF;
  SET seq_page_cost TO 10000000;

  -- uses my_idx_5
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "a": [
      {"a": "b"}, "c", 1
    ],
    "b": {
      "c": {"d": [1,2,3]},
      "e": {"f": 0}
    },
    "c": {"$exists": 1}
  }
  ';

  -- cannot use my_idx_5 since "b.e" is missing
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "a": [
      {"a": "b"}, "c", 1
    ],
    "b": {
      "c": {"d": [1,2,3]}
    },
    "c": {"$exists": 1}
  }
  ';

  -- cannot use my_idx_5 since the filter on "c" is missing
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "a": [
      {"a": "b"}, "c", 1
    ],
    "b": {
      "c": {"d": [1,2,3]},
      "e": {"f": 0}
    }
  }
  ';

  -- uses my_idx_5 even if we added a filter on "another_field"
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "$and": [
      { "b": { "c": { "d": [1,2,3] }, "e": { "f": 0 } } },
      { "c": { "$exists": 1 } },
      { "another_field" : 6},
      { "a": [ { "a": "b" }, "c", 1 ] }
    ]
  }
  ';

  -- cannot use my_idx_5 due to order of the fields under "b"
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "$and": [
      { "b": { "e": { "f": 0 } }, "c": { "d": [1,2,3] } },
      { "c": { "$exists": 1 } },
      { "another_field" : 6},
      { "a": [ { "a": "b" }, "c", 1 ] }
    ]
  }
  ';

  -- cannot use my_idx_5 due to order of the elements in array "b.c.d"
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb_4', 'collection_10')
  WHERE document @@ '
  {
    "$and": [
      { "b": { "e": { "f": 0 } }, "c": { "d": [1,3,2] } },
      { "c": { "$exists": 1 } },
      { "another_field" : 6},
      { "a": [ { "a": "b" }, "c", 1 ] }
    ]
  }
  ';
COMMIT;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"h.$**": 1}, "name": "my_idx_6",
         "partialFilterExpression": {}
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_6');

-- this is normally not supported due to $or
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_10",
     "indexes": [
       {
         "key": {"z.$**": 1}, "name": "my_idx_7",
         "partialFilterExpression": {
           "$and": [
             {"$or": [{"a": 1}]}
           ]
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_10', 'my_idx_7');
