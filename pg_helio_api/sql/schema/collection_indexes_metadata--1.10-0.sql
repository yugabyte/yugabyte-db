
/*
 * If you decide making any changes to this "type", consider syncing
 *  - __API_SCHEMA_INTERNAL__.index_spec_as_bson()
 *  - IndexSpec struct defined in index.h
 * , and make sure to add necessary constraints to index_spec_type "domain".
 */
CREATE TYPE __API_CATALOG_SCHEMA__.index_spec_type_internal AS (
    -- Mongo index name
    index_name text,

    --
    -- index options start here
    --

    -- "key" document used when creating the index
    index_key __CORE_SCHEMA__.bson,

    -- "partialFilterExpression" document used when creating the index
    index_pfe __CORE_SCHEMA__.bson,

    -- Normalized form of "wildcardProjection" document used when creating the index.
    -- "null" if the index doesn't have a "wildcardProjection" specification.
    index_wp __CORE_SCHEMA__.bson,

    -- indicates if the index was created as sparse index or not
    index_is_sparse bool,

    -- indicates if the index was created as a unique index or not.
    index_is_unique bool,

    -- version of the indexing engine used to create this index
    index_version int,

    -- document expiry threshold for ttl index
    index_expire_after_seconds int
);

CREATE DOMAIN __API_CATALOG_SCHEMA__.index_spec_type AS __API_CATALOG_SCHEMA__.index_spec_type_internal
CHECK ((VALUE).index_version IS NOT NULL AND
       (VALUE).index_version > 0 AND
       (VALUE).index_name IS NOT NULL AND
       (VALUE).index_key IS NOT NULL AND
       ((VALUE).index_expire_after_seconds IS NULL OR (VALUE).index_expire_after_seconds >= 0));

CREATE SEQUENCE __API_CATALOG_SCHEMA__.collection_indexes_index_id_seq AS integer START WITH 1; -- let 0 mean invalid, see index.h/INVALID_INDEX_ID

/*
 * i)  Maps Mongo indexes to helioapi index id's
 * ii) Does book-keeping for index keys, options etc.
 */
CREATE TABLE __API_CATALOG_SCHEMA__.collection_indexes (
    -- Postgres side collection id that corresponds to
    -- __API_CATALOG_SCHEMA__.collections(database_name, collection_name)
    collection_id bigint not null,

    -- Postgres side index id assigned to this Mongo index
    index_id integer default __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_index_id)(),

    -- Mongo index spec
    index_spec __API_CATALOG_SCHEMA__.index_spec_type not null,

    -- The __API_SCHEMA__.create_indexes() command that attempted
    -- to create the index got completed successfully ?
    --
    -- This either means that creation of this index is still in
    -- progress or __API_SCHEMA__.create_indexes() command failed but
    -- couldn't perform the clean-up for the indexes that it left
    -- behind.
    index_is_valid bool not null,

    PRIMARY KEY (index_id)
);

-- Speeds up queries based on index_name.
CREATE INDEX collection_index_name
ON __API_CATALOG_SCHEMA__.collection_indexes (collection_id, ((index_spec).index_name));

-- Speeds up queries based on index_key.
CREATE INDEX collection_index_key
ON __API_CATALOG_SCHEMA__.collection_indexes (collection_id, ((index_spec).index_key));
