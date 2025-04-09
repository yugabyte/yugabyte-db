-- for Unique indexes, we need to add shard key into the unique index filter
-- To ensure that we have these, we pull in the uuid operator class
-- similar to the btree_gin extension.
CREATE OPERATOR CLASS __API_CATALOG_SCHEMA__.bson_rum_exclusion_ops
    FOR TYPE __API_CATALOG_SCHEMA__.shard_key_and_document USING __EXTENSION_OBJECT__(_rum) AS
        -- we only add Btree equal (since we don't need to support others for Unique)
        OPERATOR	3	__API_CATALOG_SCHEMA__.=	,
        FUNCTION	1	uuid_cmp(uuid,uuid),
        FUNCTION	2	__API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_extract_value(__API_CATALOG_SCHEMA__.shard_key_and_document, internal),
        FUNCTION	3	__API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_extract_query(__API_CATALOG_SCHEMA__.shard_key_and_document, internal, int2, internal, internal, internal, internal),
        FUNCTION	4	__API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_consistent(internal, int2, anyelement, int4, internal, internal),
        FUNCTION    7   (__API_CATALOG_SCHEMA__.shard_key_and_document) __API_CATALOG_SCHEMA__.gin_bson_exclusion_pre_consistent(internal,smallint,__API_CATALOG_SCHEMA__.shard_key_and_document,int,internal,internal,internal,internal),
        FUNCTION    11  (__API_CATALOG_SCHEMA__.shard_key_and_document) __API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_options(internal),
    STORAGE		 uuid;