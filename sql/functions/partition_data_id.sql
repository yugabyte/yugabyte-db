CREATE FUNCTION partition_data_id(p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval bigint DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true) 
    RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_control_type              text;
v_current_partition_name    text;
v_epoch                     text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_id          bigint;
v_min_partition_id          bigint;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_start_control             bigint;
v_total_rows                bigint := 0;

BEGIN
/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */

SELECT partition_interval::bigint
    , control
    , epoch
INTO v_partition_interval
    , v_control
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND partition_type = 'partman';
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for non-native partitioning for given table:  %', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

IF v_control_type <> 'id' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    RAISE EXCEPTION 'Control column for given partition set is not id/serial based or epoch flag is set for time-based partitioning.';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        v_partition_id := ARRAY[v_min_partition_id];
        -- Check if custom batch interval overflows current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_id + v_partition_interval) THEN
            v_max_partition_id := v_min_partition_id + v_partition_interval;
        ELSE
            v_max_partition_id := v_start_control + p_batch_interval;
        END IF;

    ELSIF p_order = 'DESC' THEN
        EXECUTE 'SELECT max('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_id := v_min_partition_id + v_partition_interval;
        v_partition_id := ARRAY[v_min_partition_id];
        -- Make sure minimum doesn't underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_id THEN
            v_min_partition_id = v_start_control - p_batch_interval;
        END IF;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    -- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                v_sql := format('SELECT * FROM ONLY %I.%I WHERE %I >= %s AND %I < %s FOR UPDATE NOWAIT'
                                , v_parent_schema
                                , v_parent_tablename
                                , v_control
                                , v_min_partition_id
                                , v_control
                                , v_max_partition_id);
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

    PERFORM @extschema@.create_partition_id(p_parent_table, v_partition_id, p_analyze);
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_min_partition_id::text, TRUE);

    EXECUTE format('WITH partition_data AS (
                        DELETE FROM ONLY %I.%I WHERE %I >= %s AND %I < %s RETURNING *)
                    INSERT INTO %I.%I SELECT * FROM partition_data'
                , v_parent_schema
                , v_parent_tablename
                , v_control
                , v_min_partition_id
                , v_control
                , v_max_partition_id
                , v_parent_schema
                , v_current_partition_name);

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP;

PERFORM @extschema@.create_function_id(p_parent_table);

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

END
$$;

