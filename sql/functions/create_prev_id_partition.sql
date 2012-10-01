CREATE FUNCTION create_prev_id_partition(p_parent_table text, p_batch int DEFAULT 1) RETURNS bigint
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
v_type                      text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic')
INTO v_type, v_part_interval, v_control;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
IF v_min_control IS NULL THEN
    RETURN 0;
END IF;

v_min_partition_id = v_min_control - (v_min_control % v_part_interval);

-- Subtract 1 so that batch count number actually makes sense
FOR i IN 0..p_batch-1 LOOP
    v_partition_id := ARRAY[v_min_partition_id + (v_part_interval*i)];
RAISE NOTICE 'v_partition_id: %',v_partition_id;
    v_max_partition_id := v_min_partition_id + (v_part_interval*(i+1));
RAISE NOTICE 'v_max_partition_id: %',v_max_partition_id;

    v_sql := 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||v_part_interval||','||quote_literal(v_partition_id)||')';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_partition_id||
                ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
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
