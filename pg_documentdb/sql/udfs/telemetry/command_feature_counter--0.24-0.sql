-- This function is used to get current feature usage statistics for reporting to 
-- the telemetry pipeline
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.command_feature_counter_stats(
	IN reset_stats_after_read bool,
	OUT feature_name text,
	OUT usage_count int)
RETURNS SETOF RECORD
LANGUAGE C VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $$get_feature_counter_stats$$;