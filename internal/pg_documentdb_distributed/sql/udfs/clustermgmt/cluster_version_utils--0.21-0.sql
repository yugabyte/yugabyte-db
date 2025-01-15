
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.invalidate_cached_cluster_versions()
RETURNS void
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$invalidate_cluster_version$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.get_current_cached_cluster_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$get_current_cached_cluster_version$function$;

CREATE FUNCTION __API_SCHEMA_INTERNAL_V2__.__CONCAT_NAME_FUNCTION__(update_, __EXTENSION_OBJECT_PREFIX_V2__, _version_data())
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
 RAISE NOTICE 'Invalidating cached cluster version data';
 PERFORM __API_SCHEMA_INTERNAL_V2__.invalidate_cached_cluster_versions();
 RETURN NULL;
END;
$function$;
