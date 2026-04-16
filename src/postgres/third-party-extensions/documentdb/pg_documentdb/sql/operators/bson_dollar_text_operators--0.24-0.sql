CREATE OPERATOR __API_CATALOG_SCHEMA__.@#% (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = tsquery,
    PROCEDURE = __API_SCHEMA_INTERNAL_V2__.bson_text_tsquery,
    RESTRICT = __CORE_SCHEMA__.bson_operator_selectivity
);