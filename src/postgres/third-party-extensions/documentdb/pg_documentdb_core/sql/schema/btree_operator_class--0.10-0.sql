CREATE OPERATOR CLASS __CORE_SCHEMA__.bson_btree_ops
    DEFAULT FOR TYPE __CORE_SCHEMA__.bson USING btree AS
        OPERATOR 1 __CORE_SCHEMA__.< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 2 __CORE_SCHEMA__.<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 3 __CORE_SCHEMA__.= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 4 __CORE_SCHEMA__.>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 5 __CORE_SCHEMA__.> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __CORE_SCHEMA__.bson_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson);