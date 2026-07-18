
CREATE OPERATOR __API_CATALOG_SCHEMA__.#= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_eq
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gt
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#>= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gte
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#< (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lt
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#<= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lte
);