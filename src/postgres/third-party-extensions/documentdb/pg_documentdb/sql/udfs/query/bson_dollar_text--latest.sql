

-- dummy function that accepts the query planner phase, and exists only to handle the support function handling
-- to push down to the index.
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_text(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
 COST 10000000000 -- really dissuade the planner from using this and force the index.
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_text$function$;

-- dummy function that exists to support the index operator (below). Needs to be separate from above due to the
-- 'tsquery' being an input. We can't generate the tsquery during the planning phase as we must use the default_language
-- in the index definition as STOP words defaults. Consequently the planner generates the function above, then the support
-- generates the operator below.
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_text_tsquery(__CORE_SCHEMA__.bson, tsquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_text$function$;

-- This is a function that tracks the input tsquery, and the index options associated with it so that $meta queries
-- can be made to work - injected during the plan phase.
-- Also used to do a runtime $text scan if necessary.
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_text_meta_qual(__CORE_SCHEMA__.bson, tsquery, bytea, bool)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_text_meta_qual$function$;


DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.bson_query_to_tsquery;

-- This is a diagnostic query used to test translation of $text search clauses to TSQuery in Postgres.
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_query_to_tsquery(query __CORE_SCHEMA__.bson, textSearch text default NULL)
 RETURNS tsquery
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$command_bson_query_to_tsquery$function$;