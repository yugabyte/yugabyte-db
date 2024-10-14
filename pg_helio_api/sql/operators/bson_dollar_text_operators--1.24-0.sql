CREATE OPERATOR __API_CATALOG_SCHEMA__.@#% (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = tsquery,
    PROCEDURE = helio_api_internal.bson_text_tsquery,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);