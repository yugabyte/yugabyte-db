/*
 * Returns true if index options provided by given two index specs are
 * equivalent, i.e.: compares everything except Mongo index names.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.index_spec_options_are_equivalent(
    left_index_spec __API_CATALOG_SCHEMA__.index_spec_type,
    right_index_spec __API_CATALOG_SCHEMA__.index_spec_type)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$index_spec_options_are_equivalent$function$;
