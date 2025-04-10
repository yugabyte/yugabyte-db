CREATE OPERATOR __API_CATALOG_SCHEMA__.@|-| (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_geowithin,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.@|#| (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_dollar_geointersects,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);

CREATE OPERATOR __API_CATALOG_SCHEMA__.<|-|> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = __API_CATALOG_SCHEMA__.bson_geonear_distance
);
