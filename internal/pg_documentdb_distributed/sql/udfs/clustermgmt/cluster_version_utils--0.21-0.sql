
CREATE OR REPLACE FUNCTION helio_api_internal.invalidate_cached_cluster_versions()
RETURNS void
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$invalidate_cluster_version$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.get_current_cached_cluster_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$get_current_cached_cluster_version$function$;

CREATE FUNCTION helio_api_internal.update_helio_version_data()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
 RAISE NOTICE 'Invalidating cached cluster version data';
 PERFORM helio_api_internal.invalidate_cached_cluster_versions();
 RETURN NULL;
END;
$function$;
