
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_sum_avg_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_sum_avg_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_sum_avg_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_sum_avg_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_sum_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_sum_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_avg_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_avg_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_min_max_final(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_min_max_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_max_transition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_max_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_min_transition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_min_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_min_combine(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_min_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_max_combine(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_max_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_first_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[])
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_last_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[])
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_last_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_first_transition_on_sorted(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_last_transition_on_sorted(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_last_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_first_last_final_on_sorted(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_last_final_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_first_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE STRICT
AS 'MODULE_PATHNAME', $function$bson_first_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_last_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE STRICT
AS 'MODULE_PATHNAME', $function$bson_last_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_first_last_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_last_final$function$;

/*
 * TODO: Replace this in favor of the new approach below.
 */
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_array_agg_transition(bytea, __CORE_SCHEMA__.bson, text)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_array_agg_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_array_agg_transition(bytea, __CORE_SCHEMA__.bson, text, boolean)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_array_agg_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_array_agg_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_array_agg_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_object_agg_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_object_agg_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_object_agg_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_object_agg_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_out_transition(bytea, __CORE_SCHEMA__.bson, text, text, text, text)
 RETURNS bytea
 LANGUAGE c
 VOLATILE
AS 'MODULE_PATHNAME', $function$bson_out_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_out_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 VOLATILE
AS 'MODULE_PATHNAME', $function$bson_out_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_firstn_transition(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[])
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_lastn_transition(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[])
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_lastn_transition$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_firstn_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_lastn_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_lastn_combine$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_firstn_lastn_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_lastn_final$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_firstn_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, bigint)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_lastn_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, bigint)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_lastn_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_firstn_lastn_final_on_sorted(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_lastn_final_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_add_to_set_transition(bytea, __CORE_SCHEMA_V2__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_add_to_set_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_add_to_set_final(bytea)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_add_to_set_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_merge_objects_transition_on_sorted(bytea, __CORE_SCHEMA_V2__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_merge_objects_transition_on_sorted$function$;

/*
 * This can't use __CORE_SCHEMA_V2__.bson due to citus type checks. We can migrate once the underlying tuples use the new types.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_merge_objects_transition(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_merge_objects_transition$function$;

/*
 * This can't use __CORE_SCHEMA_V2__.bson due to citus type checks. We can migrate once the underlying tuples use the new types.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_merge_objects_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_merge_objects_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_pop_samp_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_pop_samp_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_pop_samp_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_pop_samp_combine$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_pop_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_pop_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_std_dev_samp_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_std_dev_samp_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_first_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_last_transition(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_last_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_firstn_transition(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_lastn_transition(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_lastn_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_maxn_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_maxn_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_minn_transition(bytea, __CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_minn_transition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_maxminn_final(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_maxminn_final$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_maxminn_combine(bytea, bytea)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_maxminn_combine$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_first_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_first_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_last_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_last_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_firstn_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_firstn_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_lastn_transition_on_sorted(bytea, __CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS bytea
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$bson_lastn_transition_on_sorted$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_add_double(internal, __CORE_SCHEMA__.bson, int4, __CORE_SCHEMA__.bson)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE
AS 'MODULE_PATHNAME', $function$tdigest_add_double$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_add_double_array(internal, __CORE_SCHEMA__.bson, int4, __CORE_SCHEMA__.bson)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE
AS 'MODULE_PATHNAME', $function$tdigest_add_double_array$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_percentile(internal)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE
AS 'MODULE_PATHNAME', $function$tdigest_percentile$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_array_percentiles(internal)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE
AS 'MODULE_PATHNAME', $function$tdigest_array_percentiles$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_combine(internal, internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE
AS 'MODULE_PATHNAME', $function$tdigest_combine$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_serial(internal)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$tdigest_serial$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.tdigest_deserial(bytea, internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$tdigest_deserial$function$;