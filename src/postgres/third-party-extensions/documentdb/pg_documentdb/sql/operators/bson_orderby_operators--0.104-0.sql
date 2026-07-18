-- These are the BSON ordering comparison operators that support collation.

-- Used for ordering BSON values in ascending order
CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.<<< (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson, 
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_orderby_lt,
    COMMUTATOR = OPERATOR(__API_SCHEMA_INTERNAL_V2__.>>>)
);

-- Used for equality comparison of BSON values
CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.=== (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson, 
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_orderby_eq,
    COMMUTATOR = OPERATOR(__API_SCHEMA_INTERNAL_V2__.===)
);

-- Used for ordering BSON values in descending order
CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.>>> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson, 
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_orderby_gt,
    COMMUTATOR = OPERATOR(__API_SCHEMA_INTERNAL_V2__.<<<)
);