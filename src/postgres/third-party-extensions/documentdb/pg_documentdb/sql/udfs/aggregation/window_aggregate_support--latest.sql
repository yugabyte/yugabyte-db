CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_sum_avg_minvtransition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_sum_avg_minvtransition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_array_agg_minvtransition(bytea, __CORE_SCHEMA__.bson, text, boolean)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_array_agg_minvtransition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_combine$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_invtransition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_samp_invtransition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_pop_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_covariance_samp_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_covariance_samp_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_rank()
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_rank$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dense_rank()
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_dense_rank$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_exp_moving_avg(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, boolean)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_exp_moving_avg$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_linear_fill(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_linear_fill$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_locf_fill(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_locf_fill$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_document_number()
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_document_number$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_shift(__CORE_SCHEMA__.bson, integer, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_shift$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_derivative_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bigint)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_derivative_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_integral_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bigint)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_integral_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_integral_derivative_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_integral_derivative_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_pop_samp_winfunc_invtransition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_pop_samp_winfunc_invtransition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_pop_winfunc_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_pop_winfunc_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_samp_winfunc_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_samp_winfunc_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_const_fill(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson 
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_const_fill$function$;