CREATE FUNCTION create_prev_time_partition(p_parent_table text, p_batch int DEFAULT 1) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition_name       text;
v_max_partition_timestamp   timestamp;
v_min_control               timestamp;
v_min_partition_timestamp   timestamp;
v_part_interval             interval;
v_partition_timestamp       timestamp[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
IF v_min_control IS NULL THEN
    RETURN 0;
END IF;

CASE
    WHEN v_part_interval = '15 mins' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
            '15min'::interval * floor(date_part('minute', v_min_control) / 15.0);
    WHEN v_part_interval = '30 mins' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
            '30min'::interval * floor(date_part('minute', v_min_control) / 30.0);
    WHEN v_part_interval = '1 hour' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control);
    WHEN v_part_interval = '1 day' THEN
        v_min_partition_timestamp := date_trunc('day', v_min_control);
    WHEN v_part_interval = '1 week' THEN
        v_min_partition_timestamp := date_trunc('week', v_min_control);
    WHEN v_part_interval = '1 month' THEN
        v_min_partition_timestamp := date_trunc('month', v_min_control);
    WHEN v_part_interval = '1 year' THEN
        v_min_partition_timestamp := date_trunc('year', v_min_control);
END CASE;

-- Subtract 1 so that batch count number actually makes sense
FOR i IN 0..p_batch-1 LOOP
    v_partition_timestamp := ARRAY[(v_min_partition_timestamp + (v_part_interval*i))::timestamp];
RAISE NOTICE 'v_partition_timestamp: %',v_partition_timestamp;
    v_max_partition_timestamp := v_min_partition_timestamp + (v_part_interval*(i+1));
RAISE NOTICE 'v_max_partition_timestamp: %',v_max_partition_timestamp;

    v_sql := 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||quote_literal(v_part_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_timestamp)||')';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    -- create_time_partition() already checks to see if the partition exists and skips creation if it does. 
    -- So this function will still work with already existing partitions to get all data moved out of parent table up to and including when
    -- pg_partman was used to set up partitioning.

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_partition_timestamp)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql;

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

RETURN v_total_rows;

END
$$;
