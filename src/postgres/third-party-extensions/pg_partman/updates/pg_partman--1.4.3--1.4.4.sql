-- Bug fix: Typos in partition_time_data/id() functions. Only ran into this if a lockwait was hit while trying to partition data.


/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE OR REPLACE FUNCTION partition_data_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_lock_wait numeric DEFAULT 0) RETURNS bigint
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
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;

BEGIN

SELECT part_interval::interval, control, datetime_string
INTO v_part_interval, v_control, v_datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic');
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

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
--    RAISE NOTICE 'v_partition_timestamp: %',v_partition_timestamp;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_timestamp + v_part_interval) THEN
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
    ELSE
        v_max_partition_timestamp := v_min_control + p_batch_interval;
    END IF;
--    RAISE NOTICE 'v_max_partition_timestamp: %',v_max_partition_timestamp;

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

    v_sql := 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||quote_literal(v_part_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_timestamp)||')';
--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';
--    RAISE NOTICE 'v_sql: %', v_sql;
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


/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE OR REPLACE FUNCTION partition_data_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval int DEFAULT NULL, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_last_partition_name       text;
v_max_partition_id          bigint;
v_min_control               bigint;
v_min_partition_id          bigint;
v_part_interval             bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;

BEGIN

SELECT part_interval::bigint, control
INTO v_part_interval, v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');
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

    v_min_partition_id = v_min_control - (v_min_control % v_part_interval);

    v_partition_id := ARRAY[v_min_partition_id];
--    RAISE NOTICE 'v_partition_id: %',v_partition_id;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_id + v_part_interval) THEN
        v_max_partition_id := v_min_partition_id + v_part_interval;
    ELSE
        v_max_partition_id := v_min_control + p_batch_interval;
    END IF;
--    RAISE NOTICE 'v_max_partition_id: %',v_max_partition_id;

-- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                v_sql := 'SELECT * FROM ONLY ' || p_parent_table ||
                ' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_id)
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

    v_sql := 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||v_part_interval||','||quote_literal(v_partition_id)||')';
--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
        DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_control||
            ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
        INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';        

--    RAISE NOTICE 'v_sql: %', v_sql;
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
