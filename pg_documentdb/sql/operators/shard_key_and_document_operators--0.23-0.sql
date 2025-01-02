CREATE OPERATOR __API_CATALOG_SCHEMA__.=
(
    LEFTARG = __API_CATALOG_SCHEMA__.shard_key_and_document,
    RIGHTARG = __API_CATALOG_SCHEMA__.shard_key_and_document,
    PROCEDURE = helio_api_internal.bson_unique_exclusion_index_equal,
    COMMUTATOR = OPERATOR(__API_CATALOG_SCHEMA__.=)
);