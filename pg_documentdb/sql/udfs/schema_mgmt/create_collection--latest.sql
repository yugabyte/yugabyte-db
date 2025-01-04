
/* SQL API function that creates a collection given a database and collection */
CREATE OR REPLACE FUNCTION helio_api.create_collection(p_database_name text, p_collection_name text)
RETURNS bool
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_create_collection_core$function$;
COMMENT ON FUNCTION helio_api.create_collection(p_database_name text, p_collection_name text)
    IS 'create a Helio API collection';
