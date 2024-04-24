
CREATE OPERATOR helio_api_internal.@!>
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_dollar_not_gt,
    RESTRICT = helio_api_internal.bson_dollar_selectivity
);

CREATE OPERATOR helio_api_internal.@!>=
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_dollar_not_gte,
    RESTRICT = helio_api_internal.bson_dollar_selectivity
);

CREATE OPERATOR helio_api_internal.@!<
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_dollar_not_lt,
    RESTRICT = helio_api_internal.bson_dollar_selectivity
);

CREATE OPERATOR helio_api_internal.@!<=
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_dollar_not_lte,
    RESTRICT = helio_api_internal.bson_dollar_selectivity
);