
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_pre_consistent(internal,smallint,__CORE_SCHEMA__.bson,int,internal,internal,internal,internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_pre_consistent$function$;


CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_exclusion_pre_consistent(internal,smallint,__API_CATALOG_SCHEMA__.shard_key_and_document,int,internal,internal,internal,internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_exclusion_pre_consistent$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_can_pre_consistent(smallint, __CORE_SCHEMA__.bson, int, internal, internal, internal)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_can_pre_consistent$function$;
