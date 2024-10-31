CREATE OPERATOR CLASS helio_api_internal.bson_rum_unique_shard_path_ops
    FOR TYPE __CORE_SCHEMA__.bson using __EXTENSION_OBJECT__(_rum) AS
        OPERATOR        1       helio_api_internal.=#= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 uuid_cmp(uuid,uuid),
        FUNCTION 2 helio_api_internal.gin_bson_unique_shard_extract_value(__CORE_SCHEMA__.bson, internal),
        FUNCTION 3 helio_api_internal.gin_bson_unique_shard_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal),
        FUNCTION 4 helio_api_internal.gin_bson_unique_shard_consistent(internal, int2, anyelement, int4, internal, internal),
        FUNCTION 7 (__CORE_SCHEMA__.bson) helio_api_internal.gin_bson_unique_shard_pre_consistent(internal,smallint,__CORE_SCHEMA__.bson,int,internal,internal,internal,internal),
    STORAGE uuid;