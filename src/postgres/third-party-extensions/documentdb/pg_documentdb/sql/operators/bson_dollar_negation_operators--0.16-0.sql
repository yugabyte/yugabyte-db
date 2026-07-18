
CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.@!>
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_dollar_not_gt,
    RESTRICT = __API_SCHEMA_INTERNAL_V2__.bson_dollar_selectivity
);

CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.@!>=
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_dollar_not_gte,
    RESTRICT = __API_SCHEMA_INTERNAL_V2__.bson_dollar_selectivity
);

CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.@!<
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_dollar_not_lt,
    RESTRICT = __API_SCHEMA_INTERNAL_V2__.bson_dollar_selectivity
);

CREATE OPERATOR __API_SCHEMA_INTERNAL_V2__.@!<=
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_dollar_not_lte,
    RESTRICT = __API_SCHEMA_INTERNAL_V2__.bson_dollar_selectivity
);