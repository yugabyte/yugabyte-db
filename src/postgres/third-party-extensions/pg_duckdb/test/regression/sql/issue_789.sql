SET duckdb.force_execution = TRUE;

COPY (SELECT 1 INTO frak UNION SELECT 2) TO '/blob';
COPY (SELECT 1 INTO frak UNION SELECT 2) TO '/blob.parquet';
