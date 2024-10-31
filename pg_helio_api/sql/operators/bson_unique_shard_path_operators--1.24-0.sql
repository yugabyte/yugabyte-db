CREATE OPERATOR helio_api_internal.=#=
(
    LEFTARG = __CORE_SCHEMA__.bson,
    RIGHTARG = __CORE_SCHEMA__.bson,
    PROCEDURE = helio_api_internal.bson_unique_shard_path_equal,
    COMMUTATOR = OPERATOR(helio_api_internal.=#=)
);