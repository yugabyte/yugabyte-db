CREATE SCHEMA regex5;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,regex5;
SET citus.next_shard_id TO 90000;
SET documentdb.next_collection_id TO 900;
SET documentdb.next_collection_index_id TO 900;

SELECT create_collection('db','regex5');

BEGIN;
set local enable_seqscan TO ON;
\i sql/regex5_tests_core.sql
ROLLBACK;

SELECT drop_collection('db','regex5');
DROP SCHEMA regex5 CASCADE;
