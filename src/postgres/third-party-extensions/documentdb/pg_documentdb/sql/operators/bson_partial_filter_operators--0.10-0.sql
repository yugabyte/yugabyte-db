
-- these are runtime operators for specific functions that are compatible
-- with partial filter expressions. For more details see comments on
-- bson_partial_filter_btree_ops.
CREATE OPERATOR __API_CATALOG_SCHEMA__.#= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_eq
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gt
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#>= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gte
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#< (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lt
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.#<= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lte
);
