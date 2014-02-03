/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition_name       text;
v_max_partition_timestamp   timestamp;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_min_control               timestamp;
v_min_partition_timestamp   timestamp;
v_part_interval             interval;
v_partition_timestamp       timestamp[];
v_rowcount                  bigint;
v_sql                       text;
v_time_position             int;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
    , last_partition
INTO v_type
    , v_part_interval
    , v_control
    , v_datetime_string
    , v_last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_part_interval THEN
    p_batch_interval := v_part_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
    IF v_min_control IS NULL THEN
        RETURN 0;
    END IF;

    IF v_type = 'time-static' OR v_type = 'time-dynamic' THEN
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
            WHEN v_part_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_min_control);
            WHEN v_part_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_min_control);
        END CASE;
    ELSIF v_type = 'time-custom' THEN
        -- Keep going backwards, checking if the time interval encompases the current v_min_control value
        v_time_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        v_min_partition_timestamp := to_timestamp(substring(v_last_partition from v_time_position), v_datetime_string);
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
        LOOP
            IF v_min_control >= v_min_partition_timestamp AND v_min_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                v_max_partition_timestamp := v_min_partition_timestamp;
                BEGIN
                    v_min_partition_timestamp := v_min_partition_timestamp - v_part_interval;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_interval;
                END;
            END IF;
        END LOOP;
            
    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF (v_min_control + p_batch_interval) >= (v_min_partition_timestamp + v_part_interval) THEN
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
    ELSE
        v_max_partition_timestamp := v_min_control + p_batch_interval;
    END IF;

-- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                v_sql := 'SELECT * FROM ONLY ' || p_parent_table ||
                ' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)
                ||' FOR UPDATE NOWAIT';
                EXECUTE v_sql;
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RETURN -1;
        END IF;
    END IF;

    v_last_partition_name := @extschema@.create_time_partition(p_parent_table, v_partition_timestamp);

    EXECUTE 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

IF v_type = 'time-static' THEN
        PERFORM @extschema@.create_time_function(p_parent_table);
END IF;    

RETURN v_total_rows;

END
$$;

