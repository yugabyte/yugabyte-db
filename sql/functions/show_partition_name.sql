/*
 * Given a parent table and partition value, return the name of the child partition it would go in.
 * If using epoch time partitioning, give the text representation of the timestamp NOT the epoch integer value (use to_timestamp() to convert epoch values).
 * Also returns just the suffix value and true if the child table exists or false if it does not
 */
CREATE FUNCTION show_partition_name(p_parent_table text, p_value text, OUT partition_table text, OUT suffix_timestamp timestamp, OUT suffix_id bigint, OUT table_exists boolean) RETURNS record
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_child_exists          text;
v_datetime_string       text;
v_max_range             timestamptz;
v_min_range             timestamptz;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    text;
v_time_position         int;
v_type                  text;

BEGIN

SELECT partition_type 
    , partition_interval
    , datetime_string
INTO v_type
    , v_partition_interval
    , v_datetime_string 
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;

IF v_type IS NULL THEN
    RAISE EXCEPTION 'Parent table given is not managed by pg_partman (%)', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);
IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Parent table given does not exist (%)', p_parent_table;
END IF;

IF v_type = 'time' THEN
    CASE
        WHEN v_partition_interval::interval = '15 mins' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp) + 
                '15min'::interval * floor(date_part('minute', p_value::timestamp) / 15.0);
        WHEN v_partition_interval::interval = '30 mins' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp) + 
                '30min'::interval * floor(date_part('minute', p_value::timestamp) / 30.0);
        WHEN v_partition_interval::interval = '1 hour' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 day' THEN
            suffix_timestamp := date_trunc('day', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 week' THEN
            suffix_timestamp := date_trunc('week', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 month' THEN
            suffix_timestamp := date_trunc('month', p_value::timestamp);
        WHEN v_partition_interval::interval = '3 months' THEN
            suffix_timestamp := date_trunc('quarter', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 year' THEN
            suffix_timestamp := date_trunc('year', p_value::timestamp);
    END CASE;
    partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, to_char(suffix_timestamp, v_datetime_string), TRUE);
ELSIF v_type = 'id' THEN
    suffix_id := (p_value::bigint - (p_value::bigint % v_partition_interval::bigint));
    partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, suffix_id::text, TRUE);
ELSIF v_type = 'time-custom' THEN

    SELECT child_table, lower(partition_range) INTO partition_table, suffix_timestamp FROM @extschema@.custom_time_partitions 
        WHERE parent_table = p_parent_table AND partition_range @> p_value::timestamptz;

    IF partition_table IS NULL THEN
        SELECT max(upper(partition_range)) INTO v_max_range FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table;
        SELECT min(lower(partition_range)) INTO v_min_range FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table;
        IF p_value::timestamp >= v_max_range THEN
            suffix_timestamp := v_max_range;
            LOOP
                -- Keep incrementing higher until given value is below the upper range
                suffix_timestamp := suffix_timestamp + v_partition_interval::interval;
                IF p_value::timestamp < suffix_timestamp THEN
                    -- Have to subtract one interval because the value would actually be in the partition previous 
                    --      to this partition timestamp since the partition names contain the lower boundary
                    suffix_timestamp := suffix_timestamp - v_partition_interval::interval;
                    EXIT;
                END IF;
            END LOOP;
        ELSIF p_value::timestamp < v_min_range THEN
            suffix_timestamp := v_min_range;
            LOOP
                -- Keep decrementing lower until given value is below or equal to the lower range
                suffix_timestamp := suffix_timestamp - v_partition_interval::interval;
                IF p_value::timestamp >= suffix_timestamp THEN
                    EXIT;
                END IF;
            END LOOP;
        ELSE
            RAISE EXCEPTION 'Unable to determine a valid child table for the given parent table and value';
        END IF;

        partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, to_char(suffix_timestamp, v_datetime_string), TRUE);
    END IF;
END IF;

SELECT tablename INTO v_child_exists
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(partition_table, '.', 1)
AND tablename = split_part(partition_table, '.', 2);

IF v_child_exists IS NOT NULL THEN
    table_exists := true;
ELSE
    table_exists := false;
END IF;

RETURN;

END
$$;

