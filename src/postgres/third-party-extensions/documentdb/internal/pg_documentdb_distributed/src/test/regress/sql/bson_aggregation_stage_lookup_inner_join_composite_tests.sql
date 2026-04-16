SET citus.next_shard_id TO 891000;
SET documentdb.next_collection_id TO 8910;
SET documentdb.next_collection_index_id TO 8910;

set documentdb.defaultUseCompositeOpClass to on;
\i sql/bson_aggregation_stage_lookup_inner_join_core.sql