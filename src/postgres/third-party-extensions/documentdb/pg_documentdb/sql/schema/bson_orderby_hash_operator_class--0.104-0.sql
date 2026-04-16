
CREATE OPERATOR CLASS __API_SCHEMA_INTERNAL_V2__.bson_orderby_hash_ops
    FOR TYPE __CORE_SCHEMA__.bson USING hash AS
        OPERATOR 1 __API_SCHEMA_INTERNAL_V2__.=== (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __CORE_SCHEMA__.bson_hash_int4(__CORE_SCHEMA__.bson),
        FUNCTION 2 __CORE_SCHEMA__.bson_hash_int8(__CORE_SCHEMA__.bson, int8);