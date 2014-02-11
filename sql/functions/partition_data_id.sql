/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval int DEFAULT NULL, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_last_partition_name       text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_id          bigint;
v_min_control               bigint;
v_min_partition_id          bigint;
v_part_interval             bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
INTO v_type
    , v_part_interval
    , v_control
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
        v_lock_iter := 0;
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

    v_last_partition_name := @extschema@.create_id_partition(p_parent_table, v_partition_id);

    EXECUTE 'WITH partition_data AS (
        DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_control||
            ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
        INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';        

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

IF v_type = 'id-static' THEN
        PERFORM @extschema@.create_id_function(p_parent_table);
END IF;    

RETURN v_total_rows;

END
$$;
