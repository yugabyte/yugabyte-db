CREATE SCHEMA regex5;
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal,public,regex5;
SET citus.next_shard_id TO 90000;
SET helio_api.next_collection_id TO 900;
SET helio_api.next_collection_index_id TO 900;

SELECT create_collection('db','regex5');

BEGIN;
set local enable_seqscan TO ON;
\i sql/regex5_tests_core.sql
ROLLBACK;

SELECT drop_collection('db','regex5');
DROP SCHEMA regex5 CASCADE;
