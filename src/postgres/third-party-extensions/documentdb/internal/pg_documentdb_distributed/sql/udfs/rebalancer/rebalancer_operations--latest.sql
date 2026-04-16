
CREATE OR REPLACE FUNCTION __API_DISTRIBUTED_SCHEMA__.rebalancer_status(p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_status$$;

 
CREATE OR REPLACE FUNCTION __API_DISTRIBUTED_SCHEMA__.rebalancer_start(p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_start$$;


CREATE OR REPLACE FUNCTION __API_DISTRIBUTED_SCHEMA__.rebalancer_stop(p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_stop$$;