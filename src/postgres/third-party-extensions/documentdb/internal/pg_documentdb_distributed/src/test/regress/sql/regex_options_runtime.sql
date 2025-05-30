CREATE SCHEMA regex_options;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,regex_options;
SET citus.next_shard_id TO 700000;
SET documentdb.next_collection_id TO 700;
SET documentdb.next_collection_index_id TO 700;

SELECT create_collection('db','regex_options');

BEGIN;
set local enable_seqscan TO ON;
\i sql/regex_options_core.sql
ROLLBACK;

SELECT drop_collection('db','regex_options');
DROP SCHEMA regex_options CASCADE;
