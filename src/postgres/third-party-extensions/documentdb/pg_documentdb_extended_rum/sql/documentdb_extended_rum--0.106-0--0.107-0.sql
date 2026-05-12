
ALTER OPERATOR FAMILY documentdb_extended_rum_catalog.bson_extended_rum_composite_path_ops USING documentdb_extended_rum ADD OPERATOR 34 documentdb_api_internal.<>-|(documentdb_core.bson, documentdb_core.bson) FOR ORDER BY documentdb_core.bson_btree_ops;
ALTER OPERATOR FAMILY documentdb_extended_rum_catalog.bson_extended_rum_composite_path_ops USING documentdb_extended_rum ADD FUNCTION 9 (documentdb_core.bson)documentdb_api_internal.bson_index_transform(bytea, bytea, int2, internal);


CREATE OPERATOR CLASS documentdb_extended_rum_catalog.bson_extended_rum_unique_shard_path_ops
    FOR TYPE documentdb_core.bson using documentdb_extended_rum AS
        OPERATOR        1       documentdb_api_internal.=#= (documentdb_core.bson, documentdb_core.bson),
        FUNCTION 1 uuid_cmp(uuid,uuid),
        FUNCTION 2 documentdb_api_internal.gin_bson_unique_shard_extract_value(documentdb_core.bson, internal),
        FUNCTION 3 documentdb_api_internal.gin_bson_unique_shard_extract_query(documentdb_core.bson, internal, int2, internal, internal, internal, internal),
        FUNCTION 4 documentdb_api_internal.gin_bson_unique_shard_consistent(internal, int2, anyelement, int4, internal, internal),
        FUNCTION 7 (documentdb_core.bson) documentdb_api_internal.gin_bson_unique_shard_pre_consistent(internal,smallint,documentdb_core.bson,int,internal,internal,internal,internal),
    STORAGE uuid;