
ALTER OPERATOR FAMILY __API_SCHEMA_INTERNAL_V2__.bson_rum_unique_shard_path_ops USING __EXTENSION_OBJECT__(_rum)
    ADD FUNCTION 11 (__CORE_SCHEMA__.bson) __API_SCHEMA_INTERNAL_V2__.gin_bson_unique_shard_options(internal);