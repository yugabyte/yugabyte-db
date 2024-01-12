
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.bson_distinct_array_agg_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_distinct_array_agg_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.bson_distinct_array_agg_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_distinct_array_agg_final$function$;


CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSON_DISTINCT_AGG(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_SCHEMA_INTERNAL__.bson_distinct_array_agg_transition,
    FINALFUNC = __API_SCHEMA_INTERNAL__.bson_distinct_array_agg_final,
    stype = bytea,
    PARALLEL = SAFE
);