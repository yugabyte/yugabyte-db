
CREATE OPERATOR = (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_equal,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = <>
);

CREATE OPERATOR <> (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_not_equal,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = =
);

CREATE OPERATOR < (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_lt,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = >=
);

CREATE OPERATOR <= (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_lte,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = >
);

CREATE OPERATOR > (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_gt,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = <=
);

CREATE OPERATOR >= (
    LEFTARG = bson,
    RIGHTARG = bson,
    PROCEDURE = bson_gte,
    RESTRICT = bson_operator_selectivity,
    NEGATOR = <
);
