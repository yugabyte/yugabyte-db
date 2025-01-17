CREATE SCHEMA regex_options;
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal,public,regex_options;
SET citus.next_shard_id TO 700000;
SET helio_api.next_collection_id TO 700;
SET helio_api.next_collection_index_id TO 700;

SELECT create_collection('db','regex_options');

BEGIN;
set local enable_seqscan TO ON;
\i sql/regex_options_core.sql
ROLLBACK;

SELECT drop_collection('db','regex_options');
DROP SCHEMA regex_options CASCADE;
