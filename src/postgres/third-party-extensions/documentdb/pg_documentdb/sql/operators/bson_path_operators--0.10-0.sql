CREATE OPERATOR __API_CATALOG_SCHEMA__.@= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_eq,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__API_CATALOG_SCHEMA__.@!=)
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@!= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_ne,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__API_CATALOG_SCHEMA__.@=)
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@< (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@<= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_lte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gt,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@>= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_gte,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@*= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_in,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__API_CATALOG_SCHEMA__.@!*=)
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@!*= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_nin,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity,
    NEGATOR = OPERATOR(__API_CATALOG_SCHEMA__.@*=)
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@~ (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_regex,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@% (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_mod,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@? (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_exists,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@@# (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_size,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@# (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_type,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@&= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_all,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@#? (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_elemmatch,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@!& (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_bits_all_clear,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@!| (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_bits_any_clear,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@& (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_bits_all_set,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@| (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_bits_any_set,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.=?= (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __CORE_SCHEMA__.bson_unique_index_equal,
    COMMUTATOR = OPERATOR(__API_CATALOG_SCHEMA__.=?=)
);

