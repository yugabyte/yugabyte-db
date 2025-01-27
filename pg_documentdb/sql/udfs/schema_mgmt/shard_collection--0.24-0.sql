
/* mongo wire protocol command to shard or reshard a collection */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.shard_collection CASCADE;
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.shard_collection(
    p_database_name text,
    p_collection_name text,
    p_shard_key __CORE_SCHEMA_V2__.bson,
    p_is_reshard bool default true)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;
 COMMENT ON FUNCTION __API_SCHEMA_V2__.shard_collection(text, text,__CORE_SCHEMA_V2__.bson,bool)
    IS 'Top level command to shard or reshard a collection';

-- create clones for the new schemas
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.shard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.reshard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_reshard_collection$$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.unshard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_unshard_collection$$;