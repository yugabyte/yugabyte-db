CREATE OPERATOR __CORE_SCHEMA__.= (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_equal,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<>)
);

CREATE OPERATOR __CORE_SCHEMA__.<> (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_not_equal,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.=)
);

CREATE OPERATOR __CORE_SCHEMA__.< (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_lt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.>=)
);

CREATE OPERATOR __CORE_SCHEMA__.<= (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_lte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.>)
);

CREATE OPERATOR __CORE_SCHEMA__.> (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_gt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<=)
);

CREATE OPERATOR __CORE_SCHEMA__.>= (
    LEFTARG = __CORE_SCHEMA__.bsonquery,
    RIGHTARG = __CORE_SCHEMA__.bsonquery,
    PROCEDURE = __CORE_SCHEMA__.bsonquery_gte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__CORE_SCHEMA__.<)
);