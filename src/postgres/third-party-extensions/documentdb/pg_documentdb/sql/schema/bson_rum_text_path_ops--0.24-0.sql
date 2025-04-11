-- This is a variant of public.rum_tsvector_ops. However, this takes 'bson' as an input so we can hook it up against our
-- index selection and handling of array paths etc. We override the extract tsvector where we take the bson, generate the tsvector
-- and generate the 'text' terms for the index (just like rum would). The query path remains identical. Note that the terms in the
-- index are still text.

CREATE OPERATOR CLASS  __API_CATALOG_SCHEMA__.bson_rum_text_path_ops
    FOR TYPE __CORE_SCHEMA__.bson using __EXTENSION_OBJECT__(_rum) AS
        OPERATOR        1       __API_CATALOG_SCHEMA__.@#% (__CORE_SCHEMA__.bson, tsquery),
        FUNCTION        1       gin_cmp_tslexeme(text, text),
        FUNCTION        2       __API_SCHEMA_INTERNAL_V2__.rum_bson_single_path_extract_tsvector(__CORE_SCHEMA__.bson,internal,internal,internal,internal),
        FUNCTION        3       public.rum_extract_tsquery(tsquery,internal,smallint,internal,internal,internal,internal),
        FUNCTION        4       public.rum_tsquery_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
        FUNCTION        5       gin_cmp_prefix(text,text,smallint,internal),
        FUNCTION        6       public.rum_tsvector_config(internal),
        FUNCTION        7       public.rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
        FUNCTION        8       public.rum_tsquery_distance(internal,smallint,tsvector,int,internal,internal,internal,internal,internal),
        FUNCTION        10      public.rum_ts_join_pos(internal, internal),
        FUNCTION        11      (__CORE_SCHEMA__.bson) __API_SCHEMA_INTERNAL_V2__.rum_bson_text_path_options(internal),
    STORAGE         text;