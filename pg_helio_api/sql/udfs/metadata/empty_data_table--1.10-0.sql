/*
 * This function returns rows from an empty table that mimics a mongo data table
 * w.r.t. the data types of the columns.
 *
 * We need this to enable scenarios that require us to return an the equvalent of a
 empty mongo collection when the mongo collection does not exists.
 *
 * The planner hook for __API_SCHEMA__.collection('db', 'collName') will call this function
 * when the desired collection is not found.
 *
 * We could have made a similar change in the __API_SCHEMA__.collection() function itself,
 * but having a different function provides better debuggability, i.e., the EXPLAIN will
 * show "Function Scan on empty_data_table collection" instead of "Function Scan on collection".
 * The latter could happen for other reasons.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.empty_data_table(
    OUT shard_key_value bigint,
    OUT object_id __CORE_SCHEMA__.bson,
    OUT document __CORE_SCHEMA__.bson,
    OUT creation_time timestamptz)
RETURNS SETOF record
AS $fn$
BEGIN
		RETURN QUERY EXECUTE format($$SELECT 0::bigint, null::__CORE_SCHEMA__.bson, null::__CORE_SCHEMA__.bson, null::timestamptz WHERE false$$);
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION __API_SCHEMA__.empty_data_table()
    IS 'mimics a data collection with 0 rows';