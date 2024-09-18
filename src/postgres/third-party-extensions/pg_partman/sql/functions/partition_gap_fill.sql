CREATE FUNCTION partition_gap_fill(p_parent_table text) RETURNS integer
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_created                     boolean;
v_children_created_count            int := 0;
v_control                           text;
v_control_type                      text;
v_current_child_start_id            bigint;
v_current_child_start_timestamp     timestamptz;
v_epoch                             text;
v_expected_next_child_id            bigint;
v_expected_next_child_timestamp     timestamptz;
v_final_child_schemaname            text;
v_final_child_start_id              bigint;
v_final_child_start_timestamp       timestamptz;
v_final_child_tablename             text;
v_interval_id                       bigint;
v_interval_time                     interval;
v_previous_child_schemaname         text;
v_previous_child_tablename          text;
v_previous_child_start_id           bigint;
v_previous_child_start_timestamp    timestamptz;
v_parent_schema                     text;
v_parent_table                      text;
v_parent_tablename                  text;
v_partition_interval                text;
v_row                               record;

BEGIN

SELECT parent_table, partition_interval, control, epoch
INTO v_parent_table, v_partition_interval, v_control, v_epoch
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;
IF v_parent_table IS NULL THEN
    RAISE EXCEPTION 'Given parent table has no configuration in pg_partman: %', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

SELECT partition_schemaname, partition_tablename 
INTO v_final_child_schemaname, v_final_child_tablename
FROM @extschema@.show_partitions(v_parent_table, 'DESC')
LIMIT 1;

IF v_control_type = 'time'  OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

    v_interval_time := v_partition_interval::interval;

    SELECT child_start_time INTO v_final_child_start_timestamp
        FROM @extschema@.show_partition_info(format('%s', v_final_child_schemaname||'.'||v_final_child_tablename), p_parent_table := v_parent_table);

    FOR v_row IN 
        SELECT partition_schemaname, partition_tablename 
        FROM @extschema@.show_partitions(v_parent_table, 'ASC')
    LOOP

        RAISE DEBUG 'v_row.partition_tablename: %, v_final_child_start_timestamp: %', v_row.partition_tablename, v_final_child_start_timestamp;

        IF v_previous_child_tablename IS NULL THEN
            v_previous_child_schemaname := v_row.partition_schemaname;
            v_previous_child_tablename := v_row.partition_tablename;
            SELECT child_start_time INTO v_previous_child_start_timestamp
                FROM @extschema@.show_partition_info(format('%s', v_previous_child_schemaname||'.'||v_previous_child_tablename), p_parent_table := v_parent_table);
            CONTINUE;
        END IF;
        
        v_expected_next_child_timestamp := v_previous_child_start_timestamp + v_interval_time;

        RAISE DEBUG 'v_expected_next_child_timestamp: %', v_expected_next_child_timestamp;

        IF v_expected_next_child_timestamp = v_final_child_start_timestamp THEN
            EXIT;
        END IF;

        SELECT child_start_time INTO v_current_child_start_timestamp
            FROM @extschema@.show_partition_info(format('%s', v_row.partition_schemaname||'.'||v_row.partition_tablename), p_parent_table := v_parent_table);

        RAISE DEBUG 'v_current_child_start_timestamp: %', v_current_child_start_timestamp;

        IF v_expected_next_child_timestamp != v_current_child_start_timestamp THEN
            v_child_created :=  @extschema@.create_partition_time(v_parent_table, ARRAY[v_expected_next_child_timestamp]); 
            IF v_child_created THEN
                v_children_created_count := v_children_created_count + 1;
                v_child_created := false;
            END IF;
            SELECT partition_schema, partition_table INTO v_previous_child_schemaname, v_previous_child_tablename
                FROM @extschema@.show_partition_name(v_parent_table, v_expected_next_child_timestamp::text);
            -- Need to stay in another inner loop until the next expected child timestamp matches the current one
            -- Once it does, exit. This means gap is filled.
            LOOP
                v_previous_child_start_timestamp := v_expected_next_child_timestamp;
                v_expected_next_child_timestamp := v_expected_next_child_timestamp + v_interval_time;
                IF v_expected_next_child_timestamp = v_current_child_start_timestamp THEN
                    EXIT;
                ELSE

        RAISE DEBUG 'inner loop: v_previous_child_start_timestamp: %, v_expected_next_child_timestamp: %, v_children_created_count: %'
                , v_previous_child_start_timestamp, v_expected_next_child_timestamp, v_children_created_count;

                    v_child_created := @extschema@.create_partition_time(v_parent_table, ARRAY[v_expected_next_child_timestamp]); 
                    IF v_child_created THEN
                        v_children_created_count := v_children_created_count + 1;
                        v_child_created := false;
                    END IF;
                END IF;
            END LOOP; -- end expected child loop
        END IF;
        
        v_previous_child_schemaname := v_row.partition_schemaname;
        v_previous_child_tablename := v_row.partition_tablename;
        SELECT child_start_time INTO v_previous_child_start_timestamp
            FROM @extschema@.show_partition_info(format('%s', v_previous_child_schemaname||'.'||v_previous_child_tablename), p_parent_table := v_parent_table);

    END LOOP; -- end time loop

ELSIF v_control_type = 'id' THEN
    
    v_interval_id := v_partition_interval::bigint;

    SELECT child_start_id INTO v_final_child_start_id
        FROM @extschema@.show_partition_info(format('%s', v_final_child_schemaname||'.'||v_final_child_tablename), p_parent_table := v_parent_table);

    FOR v_row IN 
        SELECT partition_schemaname, partition_tablename 
        FROM @extschema@.show_partitions(v_parent_table, 'ASC')
    LOOP

        RAISE DEBUG 'v_row.partition_tablename: %, v_final_child_start_id: %', v_row.partition_tablename, v_final_child_start_id;

        IF v_previous_child_tablename IS NULL THEN
            v_previous_child_schemaname := v_row.partition_schemaname;
            v_previous_child_tablename := v_row.partition_tablename;
            SELECT child_start_id INTO v_previous_child_start_id
                FROM @extschema@.show_partition_info(format('%s', v_previous_child_schemaname||'.'||v_previous_child_tablename), p_parent_table := v_parent_table);
            CONTINUE;
        END IF;
 
        v_expected_next_child_id := v_previous_child_start_id + v_interval_id;

        RAISE DEBUG 'v_expected_next_child_id: %', v_expected_next_child_id;

        IF v_expected_next_child_id = v_final_child_start_id THEN
            EXIT;
        END IF;

        SELECT child_start_id INTO v_current_child_start_id
            FROM @extschema@.show_partition_info(format('%s', v_row.partition_schemaname||'.'||v_row.partition_tablename), p_parent_table := v_parent_table);

        RAISE DEBUG 'v_current_child_start_id: %', v_current_child_start_id;

        IF v_expected_next_child_id != v_current_child_start_id THEN
            v_child_created :=  @extschema@.create_partition_id(v_parent_table, ARRAY[v_expected_next_child_id]); 
            IF v_child_created THEN
                v_children_created_count := v_children_created_count + 1;
                v_child_created := false;
            END IF;
            SELECT partition_schema, partition_table INTO v_previous_child_schemaname, v_previous_child_tablename
                FROM @extschema@.show_partition_name(v_parent_table, v_expected_next_child_id::text);
            -- Need to stay in another inner loop until the next expected child id matches the current one
            -- Once it does, exit. This means gap is filled.
            LOOP
                v_previous_child_start_id := v_expected_next_child_id;
                v_expected_next_child_id := v_expected_next_child_id + v_interval_id;
                IF v_expected_next_child_id = v_current_child_start_id THEN
                    EXIT;
                ELSE

        RAISE DEBUG 'inner loop: v_previous_child_start_id: %, v_expected_next_child_id: %, v_children_created_count: %'
                , v_previous_child_start_id, v_expected_next_child_id, v_children_created_count;

                    v_child_created := @extschema@.create_partition_id(v_parent_table, ARRAY[v_expected_next_child_id]); 
                    IF v_child_created THEN
                        v_children_created_count := v_children_created_count + 1;
                        v_child_created := false;
                    END IF;
                END IF;
            END LOOP; -- end expected child loop
        END IF;

        v_previous_child_schemaname := v_row.partition_schemaname;
        v_previous_child_tablename := v_row.partition_tablename;
        SELECT child_start_id INTO v_previous_child_start_id
            FROM @extschema@.show_partition_info(format('%s', v_previous_child_schemaname||'.'||v_previous_child_tablename), p_parent_table := v_parent_table);

    END LOOP; -- end id loop

END IF; -- end time/id if

RETURN v_children_created_count;

END
$$;

