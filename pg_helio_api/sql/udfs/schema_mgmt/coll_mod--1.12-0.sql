-- collMod database command implementation for the Mongo wire protocol
CREATE OR REPLACE FUNCTION helio_api.coll_mod(
    IN p_database_name text,
    IN p_collection_name text, 
    IN p_spec helio_core.bson)
RETURNS helio_core.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $function$command_coll_mod$function$;
COMMENT ON FUNCTION helio_api.coll_mod(text, text, helio_core.bson)
    IS 'Updates the specification of helio collection';

