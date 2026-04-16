SET citus.next_shard_id TO 112000;
SET documentdb.next_collection_id TO 11200;
SET documentdb.next_collection_index_id TO 11200;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;


-- Creating another index with the same name is not ok.
-- Note that we won't create other indexes too, even if it would be ok to create them in a separate command.
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  p_database_name=>'ind_db',
  p_arg=>'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"x.y.z": 1}, "name": "valid_index_1"},
      {"key": {"c.d.e": 1}, "name": "my_idx_5", "partialFilterExpression": { "a": { "$exists": true }}},
      {"key": {"x.y": 1}, "name": "valid_index_2", "unique": true }
    ]
  }',
  p_skip_check_collection_create=>true
);

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('ind_db', 'collection_1') ORDER BY collection_id, index_id;

\d documentdb_data.documents_11201
\d+ documentdb_data.documents_rum_index_11202
\d+ documentdb_data.documents_rum_index_11203

-- stats target is 0 for the unique shard column
\d+ documentdb_data.documents_rum_index_11204

-- some tests disable background index job, let's enable it to test background index build codepath
UPDATE cron.job SET active = true WHERE jobname LIKE 'documentdb_index_%';

-- now repeat with background indexes
CALL documentdb_distributed_test_helpers.create_indexes_background(
  'ind_db',
  '{
     "createIndexes": "collection_1",
     "indexes": [
      {"key": {"back.y.z": 1}, "name": "background_valid_index_1"},
      {"key": {"back.d.e": 1}, "name": "back_my_idx_5", "partialFilterExpression": { "a": { "$exists": true }}},
      {"key": {"back.y": 1}, "name": "back_valid_index_2", "unique": true }
     ]
   }'
);

SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('ind_db', 'collection_1') ORDER BY collection_id, index_id;

\d documentdb_data.documents_11201
\d+ documentdb_data.documents_rum_index_11205
\d+ documentdb_data.documents_rum_index_11206

-- stats target is 0 for the unique shard column
\d+ documentdb_data.documents_rum_index_11207

-- create a composite unique
CALL documentdb_distributed_test_helpers.create_indexes_background(
  'ind_db',
  '{
     "createIndexes": "collection_1",
     "indexes": [
      {"key": {"back.y": 1, "back.z": 1 }, "name": "back_valid_index_3", "unique": true }
     ]
   }'
);
\d+ documentdb_data.documents_rum_index_11208

-- disable background index job
UPDATE cron.job SET active = false WHERE jobname LIKE 'documentdb_index_%';

-- create with the new operator class
set documentdb.enable_large_unique_index_keys to off;
set documentdb.forceIndexTermTruncation to on;
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  p_database_name=>'ind_db',
  p_arg=>'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"fore.y": 1, "fore.z": 1 }, "name": "fore_valid_index_3", "unique": true }
    ]
  }',
  p_skip_check_collection_create=>true
);
\d+ documentdb_data.documents_rum_index_11209

-- disable the flag - and stats shouldn't be set
set documentdb.disable_statistics_for_unique_columns to off;
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  p_database_name=>'ind_db',
  p_arg=>'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"fore.yy": 1, "fore.zz": 1 }, "name": "fore_valid_index_4", "unique": true }
    ]
  }',
  p_skip_check_collection_create=>true
);
\d+ documentdb_data.documents_rum_index_11210

set documentdb.enable_large_unique_index_keys to off;
set documentdb.forceIndexTermTruncation to off;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  p_database_name=>'ind_db',
  p_arg=>'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"fore.abc": 1, "fore.def": 1 }, "name": "fore_valid_index_no_trunc", "unique": true }
    ]
  }',
  p_skip_check_collection_create=>true
);
\d+ documentdb_data.documents_rum_index_11211
