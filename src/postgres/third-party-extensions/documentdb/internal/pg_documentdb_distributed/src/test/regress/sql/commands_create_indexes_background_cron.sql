SET citus.next_shard_id TO 320000;
SET documentdb.next_collection_id TO 32000;
SET documentdb.next_collection_index_id TO 32000;

-- Drop all the collections that are created by the create_indexes_background_core.sql
-- This ensures test stability and prevents conflicts with existing collections.
\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT documentdb_api.drop_collection('db', 'collection_6') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'createIndex_background_1') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'intermediate') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'mycol') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'constraint') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'LargeKeySize') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'UnsupportedLanguage') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'backgroundcoll1') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'backgroundcoll2') IS NOT NULL;

\o
\set ECHO :prevEcho

ALTER SYSTEM SET documentdb.indexBuildsScheduledOnBgWorker = off;
SELECT pg_reload_conf();

\i sql/create_indexes_background_core.sql

BEGIN;
-- test config update, documentdb_api_internal.schedule_background_index_build_jobs reads default guc values
SELECT FROM documentdb_api_internal.schedule_background_index_build_jobs(true);
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;

-- now set guc values and verify that they take effect
SET LOCAL documentdb.maxNumActiveUsersIndexBuilds TO 1;
-- default value for the indexBuildScheduleInSec
SELECT FROM documentdb_api_internal.schedule_background_index_build_jobs(true);
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;

SET LOCAL documentdb.maxNumActiveUsersIndexBuilds TO 1;
SET LOCAL documentdb.indexBuildScheduleInSec TO 3;
SELECT FROM documentdb_api_internal.schedule_background_index_build_jobs(true);
SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;

ROLLBACK;

SELECT schedule, jobname FROM cron.job WHERE jobname LIKE 'documentdb_index_build_task_%' ORDER BY jobId;

-- Reset -- so that other tests do not get impacted
SELECT change_index_jobs_schema.change_index_jobs_status(false);
DROP SCHEMA change_index_jobs_schema CASCADE;