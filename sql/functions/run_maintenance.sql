CREATE FUNCTION @extschema@.run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT NULL, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS void 
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_adv_lock                      boolean;
v_analyze                       boolean;
v_check_subpart                 int;
v_control_type                  text;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamptz;
v_default_tablename             text;
v_drop_count                    int := 0;
v_is_default                    text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_created        boolean;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_max_id_parent                 bigint;
v_max_time_parent               timestamptz;
v_new_search_path               text := '@extschema@,pg_temp';
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamptz;
v_old_search_path               text;
v_parent_exists                 text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_expression          text;
v_premade_count                 int;
v_premake_id_max                bigint;
v_premake_id_min                bigint;
v_premake_timestamp_min         timestamptz;
v_premake_timestamp_max         timestamptz;
v_row                           record;
v_row_max_id                    record;
v_row_max_time                  record;
v_row_sub                       record;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_id_max                    bigint;
v_sub_id_max_suffix             bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_max_suffix      timestamptz;
v_sub_timestamp_min             timestamptz;
v_tablename                     text;
v_tables_list_sql               text;

BEGIN
/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_automatic_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run on PG versions before 11. 11+ does not analyze by default anymore.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

v_row := NULL; -- Ensure it's reset

v_tables_list_sql := 'SELECT parent_table
                , partition_type
                , partition_interval
                , control
                , premake
                , undo_in_progress
                , sub_partition_set_full
                , epoch
                , infinite_time_partitions
                , retention
            FROM @extschema@.part_config
            WHERE undo_in_progress = false';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' AND automatic_maintenance = ''on''';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' AND parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;

    -- When sub-partitioning, retention may drop tables that were already put into the query loop values. 
    -- Check if they still exist in part_config before continuing
    v_parent_exists := NULL;
    SELECT parent_table INTO v_parent_exists FROM @extschema@.part_config WHERE parent_table = v_row.parent_table;
    RAISE DEBUG 'Parent table possibly removed from part_config by retenion';
    CONTINUE WHEN v_parent_exists IS NULL;
    
    -- Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
    -- Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
    -- This way at least provides a consistent way to check that I know will run. If anyone can get a working constraint/trigger, please help!
    SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = v_row.parent_table;
    IF v_sub_parent IS NOT NULL THEN
        SELECT count(*) INTO v_check_subpart FROM @extschema@.check_subpart_sameconfig(v_row.parent_table);
        IF v_check_subpart > 1 THEN
            RAISE EXCEPTION 'Inconsistent data in part_config_sub table. Sub-partition tables that are themselves sub-partitions cannot have differing configuration values among their siblings. 
            Run this query: "SELECT * FROM @extschema@.check_subpart_sameconfig(''%'');" This should only return a single row or nothing. 
            If multiple rows are returned, results are all children of the given parent. Update the differing values to be consistent for your desired values.', v_row.sub_parent;
        END IF;
    END IF;

    -- Shouldn't need to analyze tables for most statistics for native sets on PG11+ by default anymore
    IF p_analyze IS NULL THEN
        IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
            v_analyze := false;
        ELSE
            v_analyze := true;
        END IF;
    END IF;

    SELECT n.nspname, c.relname
    INTO v_parent_schema, v_parent_tablename 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_row.parent_table, '.', 1)::name
    AND c.relname = split_part(v_row.parent_table, '.', 2)::name;

    -- Used below to see if there's any data in the parent (<=PG10) or default (PG11+) child table.
    IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        -- Always returns the default partition first if it exists
        SELECT partition_tablename INTO v_default_tablename 
        FROM @extschema@.show_partitions(v_row.parent_table, p_include_default := true) LIMIT 1;

        SELECT pg_get_expr(relpartbound, v_row.parent_table::regclass) INTO v_is_default 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n on c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_default_tablename;

        IF v_is_default != 'DEFAULT' THEN
            v_default_tablename := v_parent_tablename;
        END IF;
    ELSE
        v_default_tablename := v_parent_tablename;
    END IF;

    SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_row.control);

    v_partition_expression := CASE
        WHEN v_row.epoch = 'seconds' THEN format('to_timestamp(%I)', v_row.control)
        WHEN v_row.epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_row.control)
        ELSE format('%I', v_row.control)
    END;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: v_partition_expression: %', v_partition_expression;
    END IF;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_row.epoch <> 'none') THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        SELECT child_start_time INTO v_last_partition_timestamp 
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        IF v_row.infinite_time_partitions IS TRUE THEN
            -- Set it to "now" so new partitions continue to be created
            -- For infinite_time_partitions, don't bother getting the max value in the partitions
            v_current_partition_timestamp = CURRENT_TIMESTAMP;
        ELSE 
             FOR v_row_max_time IN
               SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
            LOOP
                EXECUTE format('SELECT max(%s)::text FROM %I.%I'
                                    , v_partition_expression
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;

                IF v_current_partition_timestamp IS NOT NULL THEN
                    SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                    EXIT;
                END IF;
            END LOOP;
        END IF; -- end infinite time check

        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_default_tablename) INTO v_max_time_parent;
        IF p_debug THEN
            RAISE NOTICE 'run_maint: v_current_partition_timestamp: %, v_max_time_parent: %', v_current_partition_timestamp, v_max_time_parent;
        END IF;
        IF v_max_time_parent > v_current_partition_timestamp THEN
            SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_max_time_parent::text);
        END IF;

        IF v_current_partition_timestamp IS NULL THEN 
            -- Partition set is completely empty and infinite time partitions not set
            -- Nothing to do
            CONTINUE;
        END IF;

        -- If this is a subpartition, determine if the last child table has been made. If so, mark it as full so future maintenance runs can skip it
        SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
        IF v_sub_timestamp_max IS NOT NULL THEN
            SELECT suffix_timestamp INTO v_sub_timestamp_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_timestamp_max::text);
            IF v_sub_timestamp_max_suffix = v_last_partition_timestamp THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        -- Check and see how many premade partitions there are.
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        v_next_partition_timestamp := v_last_partition_timestamp;
        IF p_debug THEN
            RAISE NOTICE 'run_maint before loop: current_partition_timestamp: %, v_premade_count: %, v_sub_timestamp_min: %, v_sub_timestamp_max: %'
                , v_current_partition_timestamp 
                , v_premade_count
                , v_sub_timestamp_min
                , v_sub_timestamp_max;
        END IF;
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed
        WHILE (v_premade_count < v_row.premake) LOOP
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_timestamp: %', v_row.parent_table, v_premade_count, v_next_partition_timestamp;
            END IF;
            IF v_next_partition_timestamp < v_sub_timestamp_min OR v_next_partition_timestamp > v_sub_timestamp_max THEN
                -- With subpartitioning, no need to run if the timestamp is not in the parent table's range
                EXIT;
            END IF;
            BEGIN
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.partition_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', format('Child partition creation skipped for parent table: %s', v_partition_time));
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;

            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table
                                                        , ARRAY[v_next_partition_timestamp]
                                                        , v_analyze
                                                        , p_debug := p_debug); 
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
                END IF;
            END IF;

            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        END LOOP;

    ELSIF v_control_type = 'id' THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        FOR v_row_max_id IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                            , v_row.control
                            , v_row_max_id.partition_schemaname
                            , v_row_max_id.partition_tablename) INTO v_current_partition_id;
            IF v_current_partition_id IS NOT NULL THEN
                SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_id::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_default_tablename) INTO v_max_id_parent;
        IF v_max_id_parent > v_current_partition_id THEN
            SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_max_id_parent::text);
        END IF;
        IF v_current_partition_id IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        SELECT child_start_id INTO v_last_partition_id
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Determine if this table is a child of a subpartition parent. If so, get limits to see if run_maintenance even needs to run for it.
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'id');
        IF v_sub_id_max IS NOT NULL THEN
            SELECT suffix_id INTO v_sub_id_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_id_max::text);
            IF v_sub_id_max_suffix = v_last_partition_id THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        v_next_partition_id := v_last_partition_id;
        v_premade_count := ((v_last_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
        WHILE (v_premade_count < v_row.premake) LOOP 
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_id: %', v_row.parent_table, v_premade_count, v_next_partition_id;
            END IF;
            IF v_next_partition_id < v_sub_id_min OR v_next_partition_id > v_sub_id_max THEN
                -- With subpartitioning, no need to run if the id is not in the parent table's range
                EXIT;
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.partition_interval::bigint;
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], v_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
                END IF;
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

END LOOP; -- end of creation loop

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Partition maintenance finished. %s partitons made. %s partitions dropped.', v_create_count, v_drop_count));
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RUN MAINTENANCE'')', v_jobmon_schema) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;


