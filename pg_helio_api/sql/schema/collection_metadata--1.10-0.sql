
/* table that records committed writes and their logical transaction IDs */
CREATE SEQUENCE __API_CATALOG_SCHEMA__.collections_collection_id_seq AS bigint;

CREATE TABLE __API_CATALOG_SCHEMA__.collections (
    database_name text not null,
    collection_name text not null,
    collection_id bigint not null unique default __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_id)(),
    shard_key __CORE_SCHEMA__.bson,
    collection_uuid uuid,
    PRIMARY KEY (database_name, collection_name),
    CONSTRAINT valid_db_coll_name CHECK (__API_SCHEMA_INTERNAL__.ensure_valid_db_coll(database_name, collection_name))
);

