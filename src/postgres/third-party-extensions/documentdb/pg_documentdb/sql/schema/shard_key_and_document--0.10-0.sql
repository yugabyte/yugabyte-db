CREATE TYPE __API_CATALOG_SCHEMA__.shard_key_and_document AS
(
    shard_key_value int8,
    document __CORE_SCHEMA__.bson
);