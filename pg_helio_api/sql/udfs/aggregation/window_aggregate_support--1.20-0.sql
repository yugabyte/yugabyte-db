CREATE OR REPLACE FUNCTION helio_api_internal.bson_sum_avg_minvtransition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_sum_avg_minvtransition$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_array_agg_minvtransition(bytea, __CORE_SCHEMA__.bson, text, boolean)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_array_agg_minvtransition$function$;