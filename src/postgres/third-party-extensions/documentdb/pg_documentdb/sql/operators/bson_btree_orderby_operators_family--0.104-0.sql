CREATE OPERATOR FAMILY __API_SCHEMA_INTERNAL_V2__.bson_btree_orderby_operators_family USING btree;

CREATE OPERATOR CLASS __API_SCHEMA_INTERNAL_V2__.bson_orderby_compare_ops
    FOR TYPE __CORE_SCHEMA__.bson USING btree FAMILY __API_SCHEMA_INTERNAL_V2__.bson_btree_orderby_operators_family AS
        OPERATOR 1 __API_SCHEMA_INTERNAL_V2__.<<< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 3 __API_SCHEMA_INTERNAL_V2__.=== (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 5 __API_SCHEMA_INTERNAL_V2__.>>> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __API_SCHEMA_INTERNAL_V2__.bson_orderby_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);