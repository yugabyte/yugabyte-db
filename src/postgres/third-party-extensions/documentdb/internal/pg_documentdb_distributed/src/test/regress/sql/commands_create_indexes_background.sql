SET citus.next_shard_id TO 320000;
SET documentdb.next_collection_id TO 32000;
SET documentdb.next_collection_index_id TO 32000;

CREATE SCHEMA change_index_jobs_schema;
CREATE OR REPLACE FUNCTION change_index_jobs_schema.change_index_jobs_status(active_status boolean)
RETURNS void
AS $$
DECLARE
    job_id integer;
BEGIN
    FOR job_id IN (SELECT jobid FROM cron.job WHERE jobname LIKE 'documentdb_index_%' order by jobid)
    LOOP
        UPDATE cron.job SET active = active_status WHERE jobid = job_id;
        RAISE NOTICE 'Processing job_id: %', job_id;
    END LOOP;
END;
$$
LANGUAGE plpgsql;

SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,change_index_jobs_schema;
-- Delete all old create index requests from other tests
DELETE from documentdb_api_catalog.documentdb_index_queue;

---- createIndexes - top level - parse error ----
SELECT * FROM documentdb_api.create_indexes_background('db', NULL);
SELECT * FROM documentdb_api.create_indexes_background(NULL, '{}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "unknown_field": []}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": null, "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": null}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": 5}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": []}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6"}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": 1}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": 1}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [1,2]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": 1}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"name": 1}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"unique": "1"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": 1}}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"unknown_field": "111"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"name": "_name_", "unknown_field": "111"}]}');

---- createIndexes - indexes.key - parse error ----
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"": 1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**": 1, "b": -1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {".$**": 1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**": "bad"}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**": 0}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**": ""}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**": {"a": 1}}, "name": "my_idx"}]}');
SELECT index_cmd, cmd_type, index_id, index_cmd_status, collection_id, attempt, user_oid FROM documentdb_api_catalog.documentdb_index_queue ORDER BY index_id;

-- note that those are valid ..
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**foo": 1}, "name": "my_idx_1"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**": 1}, "name": "my_idx_3"}]}');

-- valid sparse index
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"asparse": 1}, "name": "my_sparse_idx1", "sparse": true}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"asparse_num": 1}, "name": "my_sparse_num_idx1", "sparse": 1}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"asparse_num": 1}, "name": "my_non_sparse_num_idx1", "sparse": 0}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.double": 1}, "name": "my_sparse_double_idx1", "sparse": 0.2}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": null, "createIndexes": "collection_6", "indexes": [{"key": 0, "key": {"bsparse": 1}, "name": "my_non_sparse_idx1", "sparse": false}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"cs.$**": 1}, "name": "my_wildcard_non_sparse_idx1", "sparse": false}]}');

-- invalid sparse indexes
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": true}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": "true"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"bs.a": 1}, "name": "my_sparse_with_pfe_idx", "sparse": true, "partialFilterExpression": { "rating": { "$gt": 5 } }}]}');

-- sparse can create index for same key with different sparse options
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_sparse_a_b_idx", "sparse": true}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx", "sparse": false}]}');

-- valid hash indexes
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": "hashed"}, "name": "my_idx_hashed"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": "hashed", "b": 1 }, "name": "my_idx_hashed_compound"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": 1, "b": "hashed" }, "name": "my_idx_hashed_compound_hash"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**": "hashed"}, "name": "my_idx_dollar_name_1"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.c$": "hashed"}, "name": "my_idx_dollar_name_2"}]}');

-- invalid hash indexes
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"b": "hashed" }, "unique": 1, "name": "invalid_hashed"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"b": "hashed", "c": "hashed" }, "name": "invalid_hashed"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"b": "hashed", "c": 1, "d": "hashed" }, "name": "invalid_hashed"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"b.$**": "hashed" }, "name": "invalid_hashed"}]}');

-- can't create index on same key with same sparse options
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx1"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx2", "sparse": false}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx3", "sparse": true}]}');

-- passing named args is also ok
SELECT * FROM documentdb_api.create_indexes_background(p_database_name=>'db', p_index_spec=>'{"createIndexes": "collection_6", "indexes": [{"key": {"c.a$**": 1}, "name": "my_idx_4"}]}');
SELECT * FROM documentdb_api.create_indexes_background(p_index_spec=>'{"createIndexes": "collection_6", "indexes": [{"key": {"d.a$**": 1}, "name": "my_idx_5"}]}', p_database_name=>'db');

-- invalid index names
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": 1 }, "name": "*"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a": 1 }, "name": "name\u0000field"}]}');

-- For the next test, show the commands that we internally execute to build
-- & clean-up the collection indexes.
SET client_min_messages TO DEBUG1;

-- Creating another index with the same name is not ok.
-- Note that we won't create other indexes too, even if it would be ok to create them in a separate command.
SELECT * FROM documentdb_api.create_indexes_background(
  p_database_name=>'db',
  p_index_spec=>'{
    "createIndexes": "collection_6",
    "indexes": [
      {"key": {"x.y.z": 1}, "name": "valid_index_1"},
      {"key": {"c.d.e": 1}, "name": "my_idx_5"},
      {"key": {"x.y": 1}, "name": "valid_index_2"}
    ]
  }');

RESET client_min_messages;

--
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**foo": 1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$**.foo": 1}, "name": "my_idx"}]}');

SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"b": -1, "a.$**": 1}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {}, "name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"name": "my_idx"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"$**foo": 1}, "name": "my_idx_13"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"..": 1}, "name": "my_idx_12"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a..b.$**": 1}, "name": "my_idx_10"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a..b.foo": 1}, "name": "my_idx_11"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"$a": 1}, "name": "my_idx_12"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {".": 1}, "name": "my_idx_12"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$b": 1}, "name": "my_idx_12"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.$b.$**": 1}, "name": "my_idx_12"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a.": 1}, "name": "my_idx_12"}]}');

-- valid mongo index type in specification, which are not supported yet
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**": "2d"}, "name": "my_idx_2d"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**": "2dsphere"}, "name": "my_idx_2dsphere"}]}');
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"a$**": "text"}, "name": "my_idx_text"}]}');

-- create a valid index.
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14"}]}');
-- creating the same index should noop
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14"}]}');
-- two index spec one already exists and another is new
SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "collection_6", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14"}, {"key": {"another_path": 1}, "name": "my_idx_15"}]}');

-- Delete all old create index requests submitted so far
DELETE from documentdb_api_catalog.documentdb_index_queue;
-- The create_indexes_background creates a remote connection via run_command_on_coordinator. Therefore, setting sequence instead of GUC.

CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "createIndex_background_1",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a"}
     ]
   }'
);

CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "createIndex_background_1",
     "indexes": [
       {"key": {"b": 1}, "name": "my_idx_b"}
     ],
     "blocking": true
   }',
   p_log_index_queue => true
);

-- Queue should be empty
SELECT index_cmd, cmd_type, index_id, index_cmd_status, collection_id, attempt, user_oid FROM documentdb_api_catalog.documentdb_index_queue ORDER BY index_id;

SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'createIndex_background_1') ORDER BY 1,2;
-- Show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM documentdb_distributed_test_helpers.get_data_table_indexes('db', 'createIndex_background_1')
ORDER BY indexrelid;

CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "createIndex_background_1",
     "indexes": [
       {"key": {"c": 1}, "name": "my_idx_c"},
       {"key": {"d": 1}, "name": "my_idx_d"}
     ]
   }',
   p_log_index_queue => true
);

-- Index request submission will fail
CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "createIndex_background_1",
     "indexes": [
       {"key": {"c": 1}, "name": "my_idx_c", "unique": true}
     ]
   }'
);
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'createIndex_background_1') ORDER BY 1,2;

-- Test unique and non-unique index creation in same request
------ check the intermediate response
SELECT documentdb_api_internal.create_indexes_background_internal(
  'db',
  '{
     "createIndexes": "intermediate",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a", "unique": true},
       {"key": {"b": 1}, "name": "my_idx_b" }
     ]
   }'
);
--- sleep for 2 seconds, so above request is processed by cron-job
SELECT pg_sleep(2);
------ check the end-to-end success flow
SELECT documentdb_api.insert_one('db','mycol', '{"_id": 1, "a" : 80, "b" : 10 }', NULL);
SELECT documentdb_api.insert_one('db','mycol', '{"_id": 2, "a" : 90, "b" : 20 }', NULL);

CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "mycol",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a", "unique": true},
       {"key": {"b": 1}, "name": "my_idx_b" }
     ]
   }'
);
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'mycol') ORDER BY 1,2;

-- Test constraint violation for unique index, insert duplicate docs
SELECT documentdb_api.insert_one('db','constraint', '{"_id": 1, "a" : 80 }', NULL);
SELECT documentdb_api.insert_one('db','constraint', '{"_id": 2, "a" : 80 }', NULL);

CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "constraint",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a", "unique": true}
     ]
   }'
);
-- try multiple such requests which are going to fail
CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "constraint",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a", "unique": true}
     ]
   }'
);

-- try multiple such requests which are going to fail
CALL documentdb_distributed_test_helpers.create_indexes_background(
  'db',
  '{
     "createIndexes": "constraint",
     "indexes": [
       {"key": {"a": 1}, "name": "my_idx_a", "unique": true}
     ]
   }'
);
-- test skippable error test
-- todo: add this test back when pgvector version upgrade is done.
-- CALL documentdb_distributed_test_helpers.create_indexes_background('db', '{ "createIndexes": "ValidateVectorSearchAsync", "indexes": [ { "name": "largeVectorIndex", "key": { "largevector": "cosmosSearch"}, "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 2001 } } ] }');
-- CALL documentdb_distributed_test_helpers.create_indexes_background('db', '{ "createIndexes": "ValidateVectorSearchAsync", "indexes": [ { "name": "largeVectorIndex", "key": { "largevector": "cosmosSearch"}, "cosmosSearchOptions": { "kind": "vector-hnsw", "similarity": "COS", "dimensions": 2001 } } ] }');

-- test another skippable error
SELECT documentdb_api.insert_one('db','LargeKeySize', '{"_id": 1, "a" : [ "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" ] }');
CALL documentdb_distributed_test_helpers.create_indexes_background('db', '{ "createIndexes": "LargeKeySize", "indexes": [ { "key" : { "a": 1 }, "name": "rumConstraint1"}] }');
CALL documentdb_distributed_test_helpers.create_indexes_background('db', '{ "createIndexes": "UnsupportedLanguage", "indexes": [ { "key": { "$**": "text" }, "name": "idx1", "default_language": "ok" } ] }');

--Reset -- so that other tests do not get impacted
SELECT change_index_jobs_schema.change_index_jobs_status(false);


-- test CheckForIndexCmdToFinish
DELETE FROM documentdb_api_catalog.documentdb_index_queue;
-- add dummy entries
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id, comment) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32101, 3, 32000, '{"err_msg" : "deadlock detected", "err_code" : { "$numberInt" : "16908292" }}');
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, global_pid, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32105, 2, 10015415, 32000);
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'R', 32105, 2, 32000);
-- this should return finish : 1, ok : 0 and error message due to index_id 32101
SELECT * FROM documentdb_api_internal.check_build_index_status('{"indexRequest" : {"cmdType" : "C", "ids" :[32101,32102,32103,32104,32105,32106]}}');
DELETE FROM documentdb_api_catalog.documentdb_index_queue;

-- test failure but no comment.
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32102, 3, 32000);
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32103, 4, 32000);
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32104, 4, 32000);
-- this should return finish : 1, ok : 0 and error message due to empty comment of failed request
SELECT * FROM documentdb_api_internal.check_build_index_status('{"indexRequest" : {"cmdType" : "C", "ids" :[32101,32102,32103,32104,32105,32106]}}');
DELETE FROM documentdb_api_catalog.documentdb_index_queue;

-- test with failed global_pid, attempt is still 1
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, global_pid, collection_id) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32105, 2, 10015415, 32000);
-- this should return finish : 0, ok : 1, it should wait for cron job to pick request again for index_id 32005 and mark attempt = 2
SELECT * FROM documentdb_api_internal.check_build_index_status('{"indexRequest" : {"cmdType" : "C", "ids" :[32101,32102,32103,32104,32105,32106]}}');
DELETE FROM documentdb_api_catalog.documentdb_index_queue;

-- test with attempt > 1
INSERT INTO documentdb_api_catalog.documentdb_index_queue (index_cmd, cmd_type, index_id, index_cmd_status, collection_id, attempt) 
VALUES ('CREATE INDEX CONCURRENTLY', 'C', 32105, 2, 32000, 2);
-- this should return finish : 1, ok : 0 and error message due to one attempt is failed "Index creation attempt failed"
SELECT * FROM documentdb_api_internal.check_build_index_status('{"indexRequest" : {"cmdType" : "C", "ids" :[32101,32102,32103,32104,32105,32106]}}');
DELETE FROM documentdb_api_catalog.documentdb_index_queue;

SELECT * FROM documentdb_api_internal.check_build_index_status('{"indexRequest" : {"cmdType" : "C", "ids" :[32101]}}');

BEGIN;
-- test config update, documentdb_api_internal.schedule_background_index_build_workers reads default guc values
SELECT FROM documentdb_api_internal.schedule_background_index_build_workers();
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;
SELECT FROM documentdb_api_internal.schedule_background_index_build_workers(1);
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;
SELECT FROM documentdb_api_internal.schedule_background_index_build_workers(1, 3);
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;

ROLLBACK;
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;
