CREATE OPERATOR CLASS __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_rum_hashed_ops)
    FOR TYPE __CORE_SCHEMA__.bson USING __EXTENSION_OBJECT__(_rum) AS
        -- hashed index only supports the equal and in operators
        OPERATOR	    1	    __API_CATALOG_SCHEMA__.@=,
        OPERATOR        6       __API_CATALOG_SCHEMA__.@*= (__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson),
        FUNCTION        1       btint8cmp(int8,int8),
        FUNCTION        2       __API_SCHEMA_INTERNAL_V2__.gin_bson_hashed_extract_value(__CORE_SCHEMA__.bson, internal),
        FUNCTION        3       __API_SCHEMA_INTERNAL_V2__.gin_bson_hashed_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal),
        FUNCTION        4       __API_SCHEMA_INTERNAL_V2__.gin_bson_hashed_consistent(internal, int2, anyelement, int4, internal, internal),
        FUNCTION        11      (__CORE_SCHEMA__.bson) __API_SCHEMA_INTERNAL_V2__.gin_bson_hashed_options(internal),
    STORAGE		 int8;