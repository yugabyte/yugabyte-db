CREATE OPERATOR helio_api_internal.@|><| (
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_geonear_within_range
);
