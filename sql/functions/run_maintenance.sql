/*
 * Function to manage pre-creation of the next partitions in a time-based partition set.
 * Also manages dropping old partitions if the retention option is set.
 */
CREATE FUNCTION run_maintenance(p_jobmon BOOLEAN DEFAULT true) RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_drop_count                    int := 0;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_timestamp      timestamp;
v_next_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 int;
v_quarter                       text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_row                           record;
v_tablename                     text;
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
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

FOR v_row IN 
SELECT parent_table
    , type
    , part_interval::interval
    , control
    , premake
    , datetime_string
    , last_partition
    , undo_in_progress
FROM @extschema@.part_config WHERE type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom'
LOOP

    CONTINUE WHEN v_row.undo_in_progress;
    
    -- Double check that last created partition exists
    IF v_row.last_partition IS NOT NULL THEN
        SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = v_row.last_partition;
        IF v_tablename IS NULL THEN
            RAISE EXCEPTION 'ERROR: Last known partition table missing for parent table %. Unable to determine next partition in sequence.', v_row.parent_table;
        END IF;
    ELSE
        RAISE EXCEPTION 'ERROR: Last known partition missing from config table for parent table %.', p_parent_table;
    END IF;

    IF v_row.type = 'time-static' OR v_row.type = 'time-dynamic' THEN
        CASE
            WHEN v_row.part_interval = '15 mins' THEN
                v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                    '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
            WHEN v_row.part_interval = '30 mins' THEN
                v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                    '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
            WHEN v_row.part_interval = '1 hour' THEN
                v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
             WHEN v_row.part_interval = '1 day' THEN
                v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
            WHEN v_row.part_interval = '1 week' THEN
                v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
            WHEN v_row.part_interval = '1 month' THEN
                v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
            WHEN v_row.part_interval = '3 months' THEN
                v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
            WHEN v_row.part_interval = '1 year' THEN
                v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
        END CASE;
    ELSIF v_row.type = 'time-custom' THEN
        SELECT child_table INTO v_current_partition FROM @extschema@.custom_time_partitions 
            WHERE parent_table = v_row.parent_table AND partition_range @> CURRENT_TIMESTAMP;
        IF v_current_partition IS NULL THEN
            RAISE EXCEPTION 'Current time partition missing from custom_time_partitions config table for table % and timestamp %',
                 CURRENT_TIMESTAMP, v_row.parent_table;
        END IF;
        v_time_position := (length(v_current_partition) - position('p_' in reverse(v_current_partition))) + 2;
        v_current_partition_timestamp := to_timestamp(substring(v_current_partition from v_time_position), v_row.datetime_string);
    END IF;

    v_time_position := (length(v_row.last_partition) - position('p_' in reverse(v_row.last_partition))) + 2;
    IF v_row.part_interval <> '3 months' OR (v_row.part_interval = '3 months' AND v_row.type = 'time-custom') THEN
       v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from v_time_position), v_row.datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(substring(v_row.last_partition from v_time_position), 'q', 1);
        v_quarter := split_part(substring(v_row.last_partition from v_time_position), 'q', 2);
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
    
    -- Check and see how many premade partitions there are.
    v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
    v_next_partition_timestamp := v_last_partition_timestamp;
    -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
    WHILE v_premade_count < v_row.premake LOOP
        BEGIN
            v_next_partition_timestamp := v_next_partition_timestamp + v_row.part_interval;
        EXCEPTION WHEN datetime_field_overflow THEN
            RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                Child partition creation skipped for parent table %', v_row.parent_table;
            v_premade_count = v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
            v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
            PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation skippd for parent table '||v_partition_time);
            CONTINUE;
        END;        
        
        v_last_partition := @extschema@.create_time_partition(v_row.parent_table, ARRAY[v_next_partition_timestamp]); 
        IF v_last_partition IS NOT NULL THEN
            UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = v_row.parent_table;
        END IF;

        v_create_count := v_create_count + 1;
        IF v_row.type = 'time-static' THEN
            PERFORM @extschema@.create_time_function(v_row.parent_table);
        END IF;

        -- Manage additonal constraints if set
        PERFORM @extschema@.apply_constraints(v_row.parent_table);
        v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
    END LOOP;

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND 
        (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'id-static' OR type = 'id-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitons made. '||v_drop_count||' partitions dropped.');
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN RUN MAINTENANCE'')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before job logging started'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


