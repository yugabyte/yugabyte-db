
CREATE OR REPLACE FUNCTION helio_api_distributed.initialize_cluster()
RETURNS void
 SET search_path TO __API_CATALOG_SCHEMA__, pg_catalog
 SET citus.enable_ddl_propagation TO on
 /*
  * By default, Citus triggers are off as there are potential pitfalls if
  * not used properly, such as, doing operations on the remote node. We use
  * them here only for local operations.
  */
 SET citus.enable_unsafe_triggers TO on
 SET citus.multi_shard_modify_mode TO 'sequential'
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_initialize_cluster$function$;


CREATE OR REPLACE FUNCTION helio_api_distributed.complete_upgrade()
RETURNS bool
    SET citus.enable_unsafe_triggers TO on
    SET citus.enable_ddl_propagation TO on
    SET citus.multi_shard_modify_mode TO 'sequential'
LANGUAGE c
STRICT
AS 'MODULE_PATHNAME', $function$command_complete_upgrade$function$;

