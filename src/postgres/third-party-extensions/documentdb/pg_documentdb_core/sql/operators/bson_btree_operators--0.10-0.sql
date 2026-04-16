
CREATE OPERATOR __CORE_SCHEMA__.= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_equal,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<>)
);

CREATE OPERATOR __CORE_SCHEMA__.<> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_not_equal,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.=)
);

CREATE OPERATOR __CORE_SCHEMA__.< (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_lt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.>=)
);

CREATE OPERATOR __CORE_SCHEMA__.<= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_lte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.>)
);

CREATE OPERATOR __CORE_SCHEMA__.> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_gt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<=)
);

CREATE OPERATOR __CORE_SCHEMA__.>= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_gte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<)
);
