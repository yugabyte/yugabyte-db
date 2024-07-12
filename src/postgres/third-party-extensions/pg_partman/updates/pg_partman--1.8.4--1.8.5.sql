-- If run_maintenance() had not been called for a while outside of a partition set's interval, running it again was not creating the necessary child partitions to catch up. Recovery from this scenario was possible if the latest partition was manually created. This update fixes the problem and if you have a partition set that was not catching up before, running it with this version installed should fix things with no further manual intervention. This bug was introduced with version 1.8.0. (Github Issue #56).


/*
 * Check if parent table is a subpartition of an already existing id based partition set managed by pg_partman
 *  If so, limit what child tables can be created based on parent suffix
 */
CREATE FUNCTION check_subpartition_limits(p_parent_table text, p_type text, OUT sub_min text, OUT sub_max text) RETURNS record
    LANGUAGE plpgsql
    AS $$
DECLARE

v_datetime_string       text;
v_id_position           int;
v_partition_interval    interval;
v_quarter               text;
v_sub_id_max            bigint;
v_sub_id_min            bigint;
v_sub_timestamp_max     timestamp;
v_sub_timestamp_min     timestamp;
v_time_position         int;
v_top_datetime_string   text;
v_top_interval          text;
v_top_parent            text;
v_top_type              text;
v_year                  text;

BEGIN

-- CTE query is done individually for each type (time, id) because it should return NULL if the top parent is not the same type in a subpartition set (id->time or time->id)

IF p_type = 'id' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname||'.'||c.relname = p_parent_table
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.part_interval, p.type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.type = 'id-static' OR p.type = 'id-dynamic';

    IF v_top_parent IS NOT NULL THEN 
        v_id_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
        v_sub_id_min := substring(p_parent_table from v_id_position)::bigint;
        v_sub_id_max := (v_sub_id_min + v_top_interval::bigint) - 1;
        sub_min := v_sub_id_min::text;
        sub_max := v_sub_id_max::text;
    END IF;

ELSIF p_type = 'time' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname||'.'||c.relname = p_parent_table
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.part_interval, p.type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.type = 'time-static' OR p.type = 'time-dynamic' OR p.type = 'time-custom';

    IF v_top_parent IS NOT NULL THEN 
        v_time_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
        IF v_top_interval::interval <> '3 months' OR (v_top_interval::interval = '3 months' AND v_top_type = 'time-custom') THEN
           v_sub_timestamp_min := to_timestamp(substring(p_parent_table from v_time_position), v_top_datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(substring(p_parent_table from v_time_position), 'q', 1);
            v_quarter := split_part(substring(p_parent_table from v_time_position), 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    v_sub_timestamp_min := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;
        v_sub_timestamp_max = (v_sub_timestamp_min + v_top_interval::interval) - '1 sec'::interval;

        sub_min := v_sub_timestamp_min::text;
        sub_max := v_sub_timestamp_max::text;
    END IF;

ELSE
    RAISE EXCEPTION 'Invalid type given as parameter to check_subpartition_limits()';
END IF;

RETURN;

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
        SELECT sub_min::timestamp, sub_max::timestamp INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');
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
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'id');
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


/*
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_partition_time (p_parent_table text, p_partition_times timestamp[], p_analyze boolean DEFAULT true) 
RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_grantees                      text[];
v_hasoids                       boolean;
v_inherit_fk                    boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_created             boolean := false;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_part_interval                 interval;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
v_row                           record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_timestamp_max             timestamp;
v_sub_timestamp_min             timestamp;
v_tablename                     text;
v_trunc_value                   text;
v_time                          timestamp;
v_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN

SELECT type
    , control
    , part_interval
    , inherit_fk
    , jobmon
    , datetime_string
INTO v_type
    , v_control
    , v_part_interval
    , v_inherit_fk
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::timestamp, sub_max::timestamp INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    
    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_part_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');
        CONTINUE;
    END;

    -- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_timestamp_min IS NOT NULL THEN
        IF v_time < v_sub_timestamp_min OR v_time > v_sub_timestamp_max THEN
            CONTINUE;
        END IF;
    END IF;

    -- This suffix generation code is in partition_data_time() as well
    v_partition_suffix := to_char(v_time, 'YYYY');
    IF v_part_interval < '1 year' AND v_part_interval <> '1 week' THEN 
        v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'MM');
        IF v_part_interval < '1 month' AND v_part_interval <> '1 week' THEN 
            v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'DD');
            IF v_part_interval < '1 day' THEN
                v_partition_suffix := v_partition_suffix || '_' || to_char(v_time, 'HH24MI');
                IF v_part_interval < '1 minute' THEN
                    v_partition_suffix := v_partition_suffix || to_char(v_time, 'SS');
                END IF; -- end < minute IF
            END IF; -- end < day IF      
        END IF; -- end < month IF
    END IF; -- end < year IF

    IF v_part_interval = '1 week' THEN
        v_partition_suffix := to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
    END IF;

    -- "Q" is ignored in to_timestamp, so handle special case
    IF v_part_interval = '3 months' AND (v_type = 'time-static' OR v_type = 'time-dynamic') THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_suffix := v_year || 'q' || v_quarter;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    SELECT relpersistence INTO v_unlogged FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || ' TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT relhasoids INTO v_hasoids FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE 'ALTER TABLE '||v_partition_name||' SET TABLESPACE '||v_parent_tablespace;
    END IF;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||v_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||v_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    -- If custom time, set extra config options.
    IF v_type = 'time-custom' THEN
        INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
        VALUES ( p_parent_table, v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
    END IF;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(quote_ident(v_parent_schema)||'.'||quote_ident(v_parent_tablename), v_partition_name);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_control
            , sub_type
            , sub_part_interval
            , sub_constraint_cols
            , sub_premake
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_jobmon
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Subpartitioning '||v_partition_name);
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_use_run_maintenance := %L
                , p_inherit_fk := %L
                , p_jobmon := %L )'
            , v_partition_name
            , v_row.sub_control
            , v_row.sub_type
            , v_row.sub_part_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
        WHERE parent_table = v_partition_name;

    END LOOP; -- end sub partitioning LOOP

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Analyzing partition set: '||p_parent_table);
    END IF;

    EXECUTE 'ANALYZE '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, 'No partitions created for partition set: '||p_parent_table);
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE TABLE: '||p_parent_table||''')' INTO v_job_id;
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
 * Function to create id partitions
 */
CREATE OR REPLACE FUNCTION create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true) RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze           boolean := FALSE;
v_control           text;
v_grantees          text[];
v_hasoids           boolean;
v_id                bigint;
v_inherit_fk        boolean;
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_grant      record;
v_parent_owner      text;
v_parent_schema     text;
v_parent_tablename  text;
v_parent_tablespace text;
v_part_interval     bigint;
v_partition_created boolean := false;
v_partition_name    text;
v_revoke            text[];
v_row               record;
v_sql               text;
v_step_id           bigint;
v_sub_id_max        bigint;
v_sub_id_min        bigint;
v_tablename         text;
v_unlogged          char;

BEGIN

SELECT control
    , part_interval
    , inherit_fk
    , jobmon
INTO v_control
    , v_part_interval
    , v_inherit_fk
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'id');

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP
-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_id_min IS NOT NULL THEN
        IF v_id < v_sub_id_min OR v_id > v_sub_id_max THEN
            CONTINUE;
        END IF;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_id::text, TRUE);
    -- If child table already exists, skip creation
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_part_interval)-1);
    END IF;

    SELECT relpersistence INTO v_unlogged FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || ' TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT relhasoids INTO v_hasoids FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE 'ALTER TABLE '||v_partition_name||' SET TABLESPACE '||v_parent_tablespace;
    END IF;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||v_control||'>='||quote_literal(v_id)||' AND '||v_control||'<'||quote_literal(v_id + v_part_interval)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(quote_ident(v_parent_schema)||'.'||quote_ident(v_parent_tablename), v_partition_name);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_control
            , sub_type
            , sub_part_interval
            , sub_constraint_cols
            , sub_premake
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_jobmon
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Subpartitioning '||v_partition_name);
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_use_run_maintenance := %L
                , p_inherit_fk := %L
                , p_jobmon := %L )'
            , v_partition_name
            , v_row.sub_control
            , v_row.sub_type
            , v_row.sub_part_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
        WHERE parent_table = v_partition_name;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

    END LOOP; -- end sub partitioning LOOP

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Analyzing partition set: '||p_parent_table);
    END IF;

    EXECUTE 'ANALYZE '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, 'No partitions created for partition set: '||p_parent_table);
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    PERFORM close_job(v_job_id);
END IF;
 
IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE TABLE: '||p_parent_table||''')' INTO v_job_id;
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

