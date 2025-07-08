-- parquet stats function
CREATE FUNCTION parquet."column_stats"("uri" TEXT) RETURNS TABLE (
	"column_id" INT,
	"field_id" INT,
	"stats_min" TEXT,
	"stats_max" TEXT,
	"stats_null_count" BIGINT,
	"stats_distinct_count" BIGINT
) STRICT
LANGUAGE c
AS 'MODULE_PATHNAME', 'column_stats_wrapper';
