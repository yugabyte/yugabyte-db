-- Fixed bug in apply_foreign_keys() that was causing it to fail if there were multiple FKs to the same foreign column. Also fixed a bug that allow the child name parameter to not be passed and didn't handle it if it wasn't. Thanks to Andrew Dunstan for the bug fix and tremendously easier to manage code (Github Issue #64).
-- Recreate the trigger function when dropping tables as part of retention to ensure those tables aren't used in the trigger condition (Github Issue #62).

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.apply_foreign_keys(text, text, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'apply_foreign_keys'; 

DROP FUNCTION @extschema@.apply_foreign_keys(text, text, boolean);

/*
 * Apply foreign keys that exist on the given parent to the given child table
 */
CREATE OR REPLACE FUNCTION apply_foreign_keys(p_parent_table text, p_child_table text, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_job_id            bigint;
v_jobmon            text;
v_jobmon_schema     text;
v_old_search_path   text;
v_ref_schema        text;
v_ref_table         text;
v_row               record;
v_schemaname        text;
v_sql               text;
v_step_id           bigint;
v_tablename         text;

BEGIN

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN APPLYING FOREIGN KEYS: '||p_parent_table);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Checking if target child table exists');
END IF;

SELECT schemaname, tablename INTO v_schemaname, v_tablename 
FROM pg_catalog.pg_tables 
WHERE schemaname||'.'||tablename = p_child_table;

IF v_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'CRITICAL', 'Target child table ('||p_child_table||') does not exist.');
        PERFORM fail_job(v_job_id);
        EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
    END IF;
    RAISE EXCEPTION 'Target child table (%.%) does not exist.', v_schemaname, v_tablename;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOR v_row IN
    SELECT pg_get_constraintdef(oid) AS constraint_def FROM pg_catalog.pg_constraint WHERE conrelid = p_parent_table::regclass AND contype = 'f'
LOOP
    v_sql := format('ALTER TABLE %I.%I ADD %s'
                    , v_schemaname
                    , v_tablename
                    , v_row.constraint_def);

    EXECUTE v_sql;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'FK applied');
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN APPLYING FOREIGN KEYS: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_run_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */
CREATE OR REPLACE FUNCTION run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT true, p_jobmon boolean DEFAULT true) RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
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
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 int;
v_quarter                       text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_step_serial_id                bigint;
v_sub_id_max                    bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_timestamp_max             timestamp;
v_sub_timestamp_min             timestamp;
v_row                           record;
v_row_max_id                    record;
v_row_sub                       record;
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
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

v_tables_list_sql := 'SELECT parent_table
                , type
                , part_interval
                , control
                , premake
                , datetime_string
                , undo_in_progress
            FROM @extschema@.part_config';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' WHERE use_run_maintenance = true';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' WHERE parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;

    SELECT show_partitions INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;

    IF v_row.type = 'time-static' OR v_row.type = 'time-dynamic' OR v_row.type = 'time-custom' THEN

        IF v_row.type = 'time-static' OR v_row.type = 'time-dynamic' THEN
            CASE
                WHEN v_row.part_interval::interval = '15 mins' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                        '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
                WHEN v_row.part_interval::interval = '30 mins' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                        '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
                WHEN v_row.part_interval::interval = '1 hour' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
                 WHEN v_row.part_interval::interval = '1 day' THEN
                    v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 week' THEN
                    v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 month' THEN
                    v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '3 months' THEN
                    v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 year' THEN
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

        -- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
        SELECT sub_min::timestamp, sub_max::timestamp INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
        -- No need to run maintenance if it's outside the bounds of the top parent. 
        IF v_sub_timestamp_min IS NOT NULL THEN
            IF v_current_partition_timestamp < v_sub_timestamp_min OR v_current_partition_timestamp > v_sub_timestamp_max THEN
                CONTINUE;
            END IF;
        END IF;

        v_time_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        IF v_row.part_interval::interval <> '3 months' OR (v_row.part_interval::interval = '3 months' AND v_row.type = 'time-custom') THEN
           v_last_partition_timestamp := to_timestamp(substring(v_last_partition from v_time_position), v_row.datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(substring(v_last_partition from v_time_position), 'q', 1);
            v_quarter := split_part(substring(v_last_partition from v_time_position), 'q', 2);
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
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.part_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation skippd for parent table '||v_partition_time);
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;

            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table, ARRAY[v_next_partition_timestamp], p_analyze); 
            v_create_count := v_create_count + 1;
            IF v_row.type = 'time-static' AND v_last_partition_created THEN
                PERFORM @extschema@.create_function_time(v_row.parent_table);
            END IF;

            -- Manage additonal constraints if set
            PERFORM @extschema@.apply_constraints(v_row.parent_table);
            -- Can be negative when subpartitioning and there are parent partitions in the past compared to current timestamp value.
            -- abs() prevents run_maintenence from running on those old parent tables
            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
        END LOOP;
    ELSIF v_row.type = 'id-static' OR v_row.type ='id-dynamic' THEN
        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
        FOR v_row_max_id IN
            SELECT show_partitions FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            EXECUTE 'SELECT '||v_row.control||' - ('||v_row.control||' % '||v_row.part_interval::int||') FROM '||v_row_max_id.show_partitions||'
                WHERE '||v_row.control||' = (SELECT max('||v_row.control||') FROM '||v_row_max_id.show_partitions||')'
                INTO v_current_partition_id;
                IF v_current_partition_id IS NOT NULL THEN
                    EXIT;
                END IF;
        END LOOP;

        -- Determine if this table is a child of a subpartition parent. If so, get limits to see if run_maintenance even needs to run for it.
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'id');
        -- No need to run maintenance if it's outside the bounds of the top parent. 
        IF v_sub_id_min IS NOT NULL THEN
            IF v_current_partition_id < v_sub_id_min OR v_current_partition_id > v_sub_id_max THEN
                CONTINUE;
            END IF;
        END IF;

        v_id_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
        v_next_partition_id := v_last_partition_id + v_row.part_interval::bigint;
        WHILE ((v_next_partition_id - v_current_partition_id) / v_row.part_interval::bigint) <= v_row.premake 
        LOOP 
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], p_analyze);
            IF v_last_partition_created THEN
                PERFORM @extschema@.create_function_id(v_row.parent_table);
                PERFORM @extschema@.apply_constraints(v_row.parent_table);
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.part_interval::bigint;
        END LOOP;

    END IF; -- end main IF check for time or id

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND 
        (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom')
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
        PERFORM @extschema@.create_function_time(v_row.parent_table);
    END IF;
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'id-static' OR type = 'id-dynamic')
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
        PERFORM @extschema@.create_function_id(v_row.parent_table);
    END IF;
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitions made. '||v_drop_count||' partitions dropped.');
    IF v_step_overflow_id IS NOT NULL OR v_step_serial_id IS NOT NULL THEN
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

-- Restore dropped object privileges
DO $$
DECLARE
v_row   record;
BEGIN
    FOR v_row IN SELECT statement FROM partman_preserve_privs_temp LOOP
        IF v_row.statement IS NOT NULL THEN
            EXECUTE v_row.statement;
        END IF;
    END LOOP;
END
$$;

DROP TABLE IF EXISTS partman_preserve_privs_temp;
