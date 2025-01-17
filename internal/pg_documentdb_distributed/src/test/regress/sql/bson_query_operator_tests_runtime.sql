
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 250000;
SET helio_api.next_collection_id TO 2500;
SET helio_api.next_collection_index_id TO 2500;

BEGIN;
\i sql/bson_query_operator_tests_core.sql
