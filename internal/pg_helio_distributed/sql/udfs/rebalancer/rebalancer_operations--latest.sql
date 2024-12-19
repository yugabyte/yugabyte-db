
CREATE OR REPLACE FUNCTION helio_api_distributed.rebalancer_status(p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_status$$;

 
CREATE OR REPLACE FUNCTION helio_api_distributed.rebalancer_start(p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_start$$;


CREATE OR REPLACE FUNCTION helio_api_distributed.rebalancer_stop(p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_rebalancer_stop$$;