SET citus.next_shard_id TO 1600000;
SET documentdb.next_collection_id TO 16000;
SET documentdb.next_collection_index_id TO 16000;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;

-- supported "partialFilterExpression" operators --

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_i",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "b": {"$gte": 10}
         }
       }
     ]
   }',
   true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('mydb', 'collection_i', 'my_idx_1');

-- not the same index since this doesn't specify partialFilterExpression
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_i",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "my_idx_1"
       }
     ]
   }',
   true
);

-- but this is the same index, so should not return an error
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_i",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "b": {"$gte": 10}
         }
       }
     ]
   }',
   true
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'mydb',
  '{
     "createIndexes": "collection_i",
     "indexes": [
       {
         "key": {"z.$**": 1}, "name": "my_idx_3",
         "partialFilterExpression":
         {
           "c": {"$type": "number" }
         }
       }
     ]
   }',
   true
);

SELECT documentdb_api.list_indexes_cursor_first_page('mydb','{ "listIndexes": "collection_i" }') ORDER BY 1;

-- force using my_idx_1 when possible
SELECT documentdb_distributed_test_helpers.drop_primary_key('mydb', 'collection_i');
SELECT SUM(1) FROM documentdb_api.insert_one('mydb','collection_i', '{"a":"foo"}'), generate_series(1, 10);

BEGIN;
  set local enable_seqscan TO OFF;
  SET seq_page_cost TO 10000000;

  -- even if filter exactly matches the partialFilterExpression of my_idx_1,
  -- cannot use the index since the index key is "a.$**"
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "b": {"$gte": 10} }
    ]
  }
  ';

  -- can use the index since it filters on the index key as well
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "b": {"$gte": 10} },
      { "a": 4 }
    ]
  }
  ';

  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "b": {"$gte": 11} },
      { "a": 4 }
    ]
  }
  ';

  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "b": {"$gt": 12} },
      { "a": 4 }
    ]
  }
  ';

  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "b": {"$eq": 13} },
      { "a": 4 }
    ]
  }
  ';

 -- cannot use index (no PFE)
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "z": {"$eq": 13} },
      { "c": { "$type": "string" } }
    ]
  }
  ';

  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "z": {"$eq": 13} },
      { "a": 4 }
    ]
  }
  ';

  -- can use index
  EXPLAIN (COSTS OFF) SELECT COUNT(*)
  FROM documentdb_api.collection('mydb', 'collection_i')
  WHERE document @@ '
  {
    "$and": [
      { "z": {"$eq": 13} },
      { "c": { "$type": "number" } }
    ]
  }
  ';
COMMIT;

-- unsupported "partialFilterExpression" operators --

CREATE FUNCTION create_index_arg_using_pfe(p_pfe documentdb_core.bson)
RETURNS documentdb_core.bson
AS $$
BEGIN
	RETURN format(
    '{
      "createIndexes": "collection_5",
      "indexes": [
        {
          "key": {"c.d": 1},
          "name": "new_idx",
          "partialFilterExpression": %s
        }
      ]
    }',
    p_pfe
  )::documentdb_core.bson;
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"$or": [{"a": 1}]}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$nor": [{"a": 1}]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$nor": [{"a": 1}, {"b": 1}]
}
'), true);


SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"b": 3}, {"a": {"$in": [2]}}, {"c": 4}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$ne": 1
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": {"$ne": 1}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": {"$nin": [1,2,3]}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"b": 3}, {"a": {"$in": 2}}, {"c": 4}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"$and": [{"a": 1}, {"b": 3}]}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": 1, "$and": [{"c": 1}]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": 1, "b": {"$and": [{"d": 1}]}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": {"$not": {"$eq": [1,2,3]}}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [{"a": {"$not": {"$eq": [1,2,3]}}}]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"a": {"$exists": true}},
    {"a": {"$size": 5}}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [{"b": 55}, {"a": {"$exists": false}}]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": "b",
  "item": {"$exists": 1, "$text": {"$search": "coffee"}},
  "c": "d"
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": {"$and": [{"a": 1}, {"b": 3}]}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [{"p": 1}, {"q": 2}],
  "b": [{"z": 1}, {"t": 2}],
  "$and": [{"p": 1}, {"q": 2}]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$and": [
    {"b": {"$gte": 1, "$lte": 3}}
  ]
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "a": {"$unknown_operator": 1}
}
'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('my_db', create_index_arg_using_pfe('
{
  "$unknown_operator": [{"a": 1}]
}
'), true);