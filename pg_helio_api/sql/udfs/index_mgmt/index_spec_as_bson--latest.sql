/* returns a bson object for given index spec */

DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL__.index_spec_as_bson;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.index_spec_as_bson(
    index_spec __API_CATALOG_SCHEMA__.index_spec_type, for_get_indexes boolean default false, namespaceName text default NULL)
RETURNS __CORE_SCHEMA__.bson 
LANGUAGE c
 IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$index_spec_as_bson$function$;