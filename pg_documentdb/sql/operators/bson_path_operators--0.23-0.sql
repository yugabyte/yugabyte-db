CREATE OPERATOR __API_CATALOG_SCHEMA__.@<> (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_dollar_range
);