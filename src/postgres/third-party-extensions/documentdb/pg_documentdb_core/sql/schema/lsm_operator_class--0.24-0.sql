/*
 * YB: The bson LSM operator class.
 */
CREATE OPERATOR CLASS __CORE_SCHEMA__.bson_lsm_ops
    DEFAULT FOR TYPE __CORE_SCHEMA__.bson USING lsm AS
        OPERATOR 1 __CORE_SCHEMA__.< (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 2 __CORE_SCHEMA__.<= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 3 __CORE_SCHEMA__.= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 4 __CORE_SCHEMA__.>= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        OPERATOR 5 __CORE_SCHEMA__.> (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 1 __CORE_SCHEMA__.bson_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION 3 __CORE_SCHEMA__.bson_in_range_numeric(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bool, bool),
        FUNCTION 3 __CORE_SCHEMA__.bson_in_range_interval(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, interval, bool, bool);
