-- Fixed search path bug with run_maintenance() introduced in v2.2.0. Was causing errors if the pg_partman schema was not in the search path of the role calling it. (Github Issue #71)


/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_run_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */
CREATE OR REPLACE FUNCTION run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT true, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_adv_lock                      boolean;
v_check_subpart                 int;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_drop_count                    int := 0;
v_id_position                   int;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_created        boolean;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamp;
v_max_id_parent                 bigint;
v_max_time_parent               timestamp;
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamp;
v_parent_schema                 text;
v_parent_tablename              text;
v_premade_count                 int;
v_premake_id_max                bigint;
v_premake_id_min                bigint;
v_premake_timestamp_min         timestamp;
v_premake_timestamp_max         timestamp;
v_quarter                       text;
v_row                           record;
v_row_max_id                    record;
v_row_max_time                  record;
v_row_sub                       record;
v_skip_maint                    boolean;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_step_serial_id                bigint;
v_sub_id_max                    bigint;
v_sub_id_max_suffix             bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_timestamp_max             timestamp;
v_sub_timestamp_max_suffix      timestamp;
v_sub_timestamp_min             timestamp;
v_tablename                     text;
v_tables_list_sql               text;
v_time_position                 int;
v_year                          text;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.add_job(%L)', v_jobmon_schema, 'PARTMAN RUN MAINTENANCE') INTO v_job_id;
    EXECUTE format('SELECT %I.add_step(%L, %L)', v_jobmon_schema, v_job_id, 'Running maintenance loop') INTO v_step_id;
END IF;

-- Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
-- Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
-- This way at least provides a consistent way to check that I know will run. If anyone can get a working constraint/trigger, please help!
-- Don't have to worry about this in the serial trigger maintenance since subpartitioning requires run_maintenance().
FOR v_row IN 
    SELECT sub_parent FROM @extschema@.part_config_sub
LOOP
    SELECT count(*) INTO v_check_subpart FROM @extschema@.check_subpart_sameconfig(v_row.sub_parent);
    IF v_check_subpart > 1 THEN
        RAISE EXCEPTION 'Inconsistent data in part_config_sub table. Sub-partition tables that are themselves sub-partitions cannot have differing configuration values among their siblings. 
        Run this query: "SELECT * FROM @extschema@.check_subpart_sameconfig(''%'');" This should only return a single row or nothing. 
        If multiple rows are returned, results are all children of the given parent. Update the differing values to be consistent for your desired values.', v_row.sub_parent;
    END IF;
END LOOP;

v_row := NULL; -- Ensure it's reset


v_tables_list_sql := 'SELECT parent_table
                , partition_type
                , partition_interval
                , control
                , premake
                , datetime_string
                , undo_in_progress
                , sub_partition_set_full
                , epoch
            FROM @extschema@.part_config
            WHERE sub_partition_set_full = false';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' AND use_run_maintenance = true';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' AND parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;
    v_skip_maint := true; -- reset every loop

    SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_row.parent_table;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_row.partition_type = 'time' OR v_row.partition_type = 'time-custom' THEN

        v_time_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        IF v_row.partition_interval::interval <> '3 months' OR (v_row.partition_interval::interval = '3 months' AND v_row.partition_type = 'time-custom') THEN
           v_last_partition_timestamp := to_timestamp(substring(v_last_partition from v_time_position), v_row.datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(substring(v_last_partition FROM v_time_position), 'q', 1);
            v_quarter := split_part(substring(v_last_partition FROM v_time_position), 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;

        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
        FOR v_row_max_time IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            IF v_row.epoch = false THEN
                EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                                    , v_row.control
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;
            ELSE
                EXECUTE format('SELECT to_timestamp(max(%I))::text FROM %I.%I'
                                    , v_row.control
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;
            END IF;
            IF v_current_partition_timestamp IS NOT NULL THEN
                SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        IF v_row.epoch = false THEN
            EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_time_parent;
        ELSE
            EXECUTE format('SELECT to_timestamp(max(%I)) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_time_parent;
        END IF;
        IF p_debug THEN
            RAISE NOTICE 'run_maint: v_current_partition_timestamp: %, v_max_time_parent: %', v_current_partition_timestamp, v_max_time_parent;
        END IF;
        IF v_max_time_parent > v_current_partition_timestamp THEN
            SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_max_time_parent::text);
        END IF;
        IF v_current_partition_timestamp IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        -- If this is a subpartition, determine if the last child table has been made. If so, mark it as full so future maintenance runs can skip it
        SELECT sub_min::timestamp, sub_max::timestamp INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
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
                    EXECUTE format('SELECT %I.add_step(%L, %L)', v_jobmon_schema, v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.') INTO v_step_overflow_id;
                    EXECUTE format('SELECT %I.update_step(%L, %L, %L)', v_jobmon_schema, v_step_overflow_id, 'CRITICAL', 'Child partition creation skippd for parent table '||v_partition_time);
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;
            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table, ARRAY[v_next_partition_timestamp], p_analyze); 
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
            END IF;

            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        END LOOP;
    ELSIF v_row.partition_type = 'id' THEN
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
        -- Check for values in the parent table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_id_parent;
        IF v_max_id_parent > v_current_partition_id THEN
            SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_max_id_parent::text);
        END IF;
        IF v_current_partition_id IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        v_id_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
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
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], p_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table := v_row.parent_table, p_job_id := v_job_id, p_debug := p_debug);

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND 
        (partition_type = 'time' OR partition_type = 'time-custom')
LOOP
    IF p_parent_table IS NULL THEN
        v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
    ELSE -- Only run retention on table given in parameter
        IF p_parent_table <> v_row.parent_table THEN
            CONTINUE;
        ELSE
            v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
        END IF;
    END IF;
    IF v_drop_count > 0 THEN
        PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
    END IF;
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND partition_type = 'id'
LOOP
    IF p_parent_table IS NULL THEN
        v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
    ELSE -- Only run retention on table given in parameter
        IF p_parent_table <> v_row.parent_table THEN
            CONTINUE;
        ELSE
            v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
        END IF;
    END IF;
    IF v_drop_count > 0 THEN
        PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
    END IF;
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.update_step(%L, %L, ''Partition maintenance finished. %s partitons made. %s partitions dropped.'')'
        , v_jobmon_schema
        , v_step_id
        , 'OK'
        , v_create_count
        , v_drop_count);
    IF v_step_overflow_id IS NOT NULL OR v_step_serial_id IS NOT NULL THEN
        EXECUTE format('SELECT %I.fail_job(%L)', v_jobmon_schema, v_job_id);
    ELSE
        EXECUTE format('SELECT %I.close_job(%L)', v_jobmon_schema, v_job_id);
    END IF;
END IF;

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

