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

CREATE OR REPLACE FUNCTION helio_api_internal.bson_covariance_pop_samp_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_transition$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_covariance_pop_samp_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_combine$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_covariance_pop_samp_invtransition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_invtransition$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_covariance_pop_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_final$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_covariance_samp_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_samp_final$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_rank()
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_rank$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_dense_rank()
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_dense_rank$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_exp_moving_avg(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, boolean)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_exp_moving_avg$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_linear_fill(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_linear_fill$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_locf_fill(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_locf_fill$function$;