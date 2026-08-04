-- Fixed issue where additional constraints would not get applied on all older partitions if more than one new partition was made for a single partition set in a single maintenance run. You can fix this by using the reapply_constraints.py script to remove then reapply all additional constraints on older tables. Or if you know the specific children that are missing a constraint, you can run apply_constraints() and pass it the child table argument. 

-- Improved performance of create_parent() when p_start_partition is set. Was previously doing a full seq scan of the parent table which could keep it locked longer than intended (Github Pull Reqest #99).

-- New function that returns partition data about a given child name: show_partition_info(). 
    -- Returns start/end values that are contained in a given child table.
    -- Can pass an optional interval value to see start/end values that are different than the currently configured interval.

-- Added new options to vacuum_maintenance.py script
    -- New --all option tells it to run against all tables managed by pg_partman.
    -- New --type option sets whether to run against time or id based partition sets managed by pg_partman.
    -- New --interval option tells the script to only run against child tables older than the given interval. See the script's --help for more info on how this works.
    -- All these options only work on partition sets managed by pg_partman. If none of these options are set, script still works on non-pg_partman inheritance sets. 

-- Disallow weekly partitioning to be a subpartition of anything else. Alignment of ISO weeks does not always match up with larger intervals and can cause constraint conflicts.
-- Also disallow subpartition interval to be equal to or greater than parent.
-- Set part_config_sub & custom_time_partition config tables to dump their contents with pg_dump to ensure all config data is saved.

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_partition_id(text, bigint[], boolean, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_partition_id'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_partition_time(text, timestamp[], boolean, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_partition_time'; 

DROP FUNCTION @extschema@.create_partition_id(text, bigint[], boolean);
DROP FUNCTION @extschema@.create_partition_time(text, timestamp[], boolean); 

SELECT pg_catalog.pg_extension_config_dump('part_config_sub', '');
SELECT pg_catalog.pg_extension_config_dump('custom_time_partitions', '');

CREATE FUNCTION show_partition_info(p_child_table text
    , p_partition_interval text DEFAULT NULL
    , p_parent_table text DEFAULT NULL
    , OUT child_start_time timestamp
    , OUT child_end_time timestamp
    , OUT child_start_id bigint
    , OUT child_end_id bigint 
    , OUT suffix text)
RETURNS record
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_child_schema          text;
v_child_tablename       text;
v_datetime_string       text;
v_parent_table          text;
v_partition_interval    text;
v_partition_type        text;
v_quarter               text;
v_suffix                text;
v_suffix_position       int;
v_year                  text;

BEGIN

SELECT schemaname, tablename INTO v_child_schema, v_child_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_child_table, '.', 1)
AND tablename = split_part(p_child_table, '.', 2);
IF v_child_tablename IS NULL THEN
    RAISE EXCEPTION 'Child table given does not exist (%)', p_child_table;
END IF;

IF p_parent_table IS NULL THEN
    SELECT n.nspname||'.'|| c.relname INTO v_parent_table
    FROM pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhparent
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhrelid::regclass = p_child_table::regclass;
ELSE
    v_parent_table := p_parent_table;
END IF;

IF p_partition_interval IS NULL THEN
    SELECT partition_interval, partition_type, datetime_string INTO v_partition_interval, v_partition_type, v_datetime_string 
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
ELSE
    v_partition_interval := p_partition_interval;
    SELECT partition_type, datetime_string INTO v_partition_type, v_datetime_string 
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
END IF;

IF v_partition_type IS NULL THEN
    RAISE EXCEPTION 'Parent table of given child not managed by pg_partman: %', v_parent_table;
END IF;

v_suffix_position := (length(v_child_tablename) - position('p_' in reverse(v_child_tablename))) + 2;
v_suffix := substring(v_child_tablename from v_suffix_position);

IF v_partition_type = 'time' OR v_partition_type = 'time-custom' THEN

        IF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
           child_start_time := to_timestamp(v_suffix, v_datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(v_suffix, 'q', 1);
            v_quarter := split_part(v_suffix, 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    child_start_time := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    child_start_time := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    child_start_time := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    child_start_time := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;

        child_end_time := (child_start_time + v_partition_interval::interval) - '1 second'::interval;

ELSIF v_partition_type = 'id' THEN

    child_start_id := v_suffix::bigint;
    child_end_id := (child_start_id + v_partition_interval::bigint) - 1;

ELSE
    RAISE EXCEPTION 'Invalid partition type encountered in show_partition_info()';
END IF;

suffix = v_suffix;

RETURN;

END
$$;


/*
 * Create a partition set that is a subpartition of an already existing partition set.
 * Given the parent table of any current partition set, it will turn all existing children into parent tables of their own partition sets
 *      using the configuration options given as parameters to this function.
 * Uses another config table that allows for turning all future child partitions into a new parent automatically.
 * To avoid logical complications and contention issues, ALL subpartitions must be maintained using run_maintenance().
 * This means the automatic, trigger based partition creation for serial partitioning will not work if it is a subpartition.
 */
CREATE OR REPLACE FUNCTION create_sub_parent(
    p_top_parent text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch boolean DEFAULT false
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_last_partition    text;
v_parent_interval   text;
v_parent_type       text;
v_row               record;
v_row_last_part     record;
v_run_maint         boolean;
v_sql               text;
v_success           boolean := false;
v_top_type          text;

BEGIN

SELECT use_run_maintenance INTO v_run_maint FROM @extschema@.part_config WHERE parent_table = p_top_parent;
IF v_run_maint IS NULL THEN
    RAISE EXCEPTION 'Cannot subpartition a table that is not managed by pg_partman already. Given top parent table not found in @extschema@.part_config: %', p_top_parent;
ELSIF v_run_maint = false THEN
    RAISE EXCEPTION 'Any parent table that will be part of a sub-partitioned set (on any level) must have use_run_maintenance set to true in part_config table, even for serial partitioning. See documentation for more info.';
END IF;

FOR v_row IN 
    -- Loop through all current children to turn them into partitioned tables
    SELECT partition_schemaname||'.'||partition_tablename AS child_table FROM @extschema@.show_partitions(p_top_parent)
LOOP
    SELECT partition_type, partition_interval INTO v_parent_type, v_parent_interval FROM @extschema@.part_config WHERE parent_table = v_row.child_table;

    IF v_parent_interval = p_interval THEN
        RAISE EXCEPTION 'Sub-partition interval cannot be equal to parent interval';
    END IF;

    IF (v_parent_type = 'time' OR v_parent_type = 'time-custom') 
       AND (p_type = 'time' OR p_type = 'time-custom') 
    THEN
        IF p_interval::interval > v_parent_interval::interval THEN
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than the given parent interval';
        END IF;
        IF p_interval = 'weekly' AND v_parent_interval::interval > '1 week'::interval THEN
            RAISE EXCEPTION 'Due to conflicting data boundaries between ISO weeks and any larger interval of time, pg_partman cannot support a sub-partition interval of weekly';
        END IF;
    ELSIF v_parent_type = 'id' THEN
        IF p_interval::bigint > v_parent_interval::bigint THEN
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than the given parent interval';
        END IF;
    END IF;
        
    -- Just call existing create_parent() function but add the given parameters to the part_config_sub table as well
    v_sql := format('SELECT @extschema@.create_parent(
             p_parent_table := %L
            , p_control := %L
            , p_type := %L
            , p_interval := %L
            , p_constraint_cols := %L
            , p_premake := %L
            , p_use_run_maintenance := %L
            , p_start_partition := %L
            , p_inherit_fk := %L
            , p_epoch := %L
            , p_jobmon := %L
            , p_debug := %L )'
        , v_row.child_table
        , p_control
        , p_type
        , p_interval
        , p_constraint_cols
        , p_premake
        , true
        , p_start_partition
        , p_inherit_fk
        , p_epoch
        , p_jobmon
        , p_debug);
    EXECUTE v_sql;

END LOOP;

INSERT INTO @extschema@.part_config_sub (
    sub_parent
    , sub_control
    , sub_partition_type
    , sub_partition_interval
    , sub_constraint_cols
    , sub_premake
    , sub_inherit_fk
    , sub_use_run_maintenance
    , sub_epoch
    , sub_jobmon)
VALUES (
    p_top_parent
    , p_control
    , p_type
    , p_interval
    , p_constraint_cols
    , p_premake
    , p_inherit_fk
    , true
    , p_epoch
    , p_jobmon);

v_success := true;

RETURN v_success;

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
                , infinite_time_partitions
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

    SELECT schemaname, tablename 
    INTO v_parent_schema, v_parent_tablename 
    FROM pg_catalog.pg_tables 
    WHERE schemaname = split_part(v_row.parent_table, '.', 1)
    AND tablename = split_part(v_row.parent_table, '.', 2);

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_row.partition_type = 'time' OR v_row.partition_type = 'time-custom' THEN

        SELECT child_start_time INTO v_last_partition_timestamp 
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);

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
        IF v_current_partition_timestamp IS NULL THEN -- Partition set is completely empty
            IF v_row.infinite_time_partitions IS TRUE THEN
                -- Set it to now so new partitions continue to be created
                v_current_partition_timestamp = CURRENT_TIMESTAMP;
            ELSE
                -- Nothing to do
                CONTINUE;
            END IF;
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
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], p_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

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
    EXECUTE format('SELECT %I.update_step(%L, %L, ''Partition maintenance finished. %s partitions made. %s partitions dropped.'')'
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


/*
 * Function to create id partitions
 */
CREATE FUNCTION create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_all                   text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze               boolean := FALSE;
v_control               text;
v_exists                text;
v_grantees              text[];
v_hasoids               boolean;
v_id                    bigint;
v_inherit_fk            boolean;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_old_search_path       text;
v_parent_grant          record;
v_parent_owner          text;
v_parent_schema         text;
v_parent_tablename      text;
v_parent_tablespace     text;
v_partition_interval    bigint;
v_partition_created     boolean := false;
v_partition_name        text;
v_revoke                text;
v_row                   record;
v_sql                   text;
v_step_id               bigint;
v_sub_id_max            bigint;
v_sub_id_min            bigint;
v_unlogged              char;

BEGIN

SELECT control
    , partition_interval
    , inherit_fk
    , jobmon
INTO v_control
    , v_partition_interval
    , v_inherit_fk
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND partition_type = 'id';

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'id');

SELECT tableowner, schemaname, tablename, tablespace 
INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP
-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_id_min IS NOT NULL THEN
        IF v_id < v_sub_id_min OR v_id > v_sub_id_max THEN
            CONTINUE;
        END IF;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_id::text, TRUE);
    -- If child table already exists, skip creation
    SELECT tablename INTO v_exists FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema AND tablename = v_partition_name;
    IF v_exists IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_partition_interval)-1);
    END IF;

    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename
    AND n.nspname = v_parent_schema;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)'
            , v_parent_schema
            , v_partition_name
            , v_parent_schema
            , v_parent_tablename);
    SELECT relhasoids INTO v_hasoids 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename
    AND n.nspname = v_parent_schema;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
    END IF;
    EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %s AND %I < %s )'
        , v_parent_schema
        , v_partition_name
        , v_partition_name||'_partition_check'
        , v_control
        , v_id
        , v_control
        , v_id + v_partition_interval);
    EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I', v_parent_schema, v_partition_name, v_parent_schema, v_parent_tablename);

    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);

    PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_infinite_time_partitions
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
                , p_epoch := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

    END LOOP; -- end sub partitioning LOOP
    
    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s', p_parent_table));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    PERFORM close_job(v_job_id);
END IF;
 
IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


/*
 * Function to create a child table in a time-based partition set
 */
CREATE FUNCTION create_partition_time(p_parent_table text, p_partition_times timestamp[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_datetime_string               text;
v_exists                        text;
v_epoch                         boolean;
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
v_partition_interval            interval;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text;
v_row                           record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_timestamp_max             timestamp;
v_sub_timestamp_min             timestamp;
v_trunc_value                   text;
v_time                          timestamp;
v_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN

SELECT partition_type
    , control
    , partition_interval
    , epoch
    , inherit_fk
    , jobmon
    , datetime_string
INTO v_type
    , v_control
    , v_partition_interval
    , v_epoch
    , v_inherit_fk
    , v_jobmon
    , v_datetime_string
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND partition_type = 'time' OR partition_type = 'time-custom';

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::timestamp, sub_max::timestamp INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');

SELECT tableowner, schemaname, tablename, tablespace 
INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    
    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_partition_interval;
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
    v_partition_suffix := to_char(v_time, v_datetime_string);
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);
    SELECT tablename INTO v_exists FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema AND tablename = v_partition_name;
    IF v_exists IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Creating new partition %s.%s with interval from %s to %s'
                                                , v_parent_schema
                                                , v_partition_name
                                                , v_partition_timestamp_start
                                                , v_partition_timestamp_end-'1sec'::interval));
    END IF;

    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename
    AND n.nspname = v_parent_schema;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)'
                                , v_parent_schema
                                , v_partition_name
                                , v_parent_schema
                                , v_parent_tablename);
    SELECT relhasoids INTO v_hasoids 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename
    AND n.nspname = v_parent_schema;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
    END IF;
    IF v_epoch = false THEN
        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                        , v_parent_schema
                        , v_partition_name
                        , v_partition_name||'_partition_check'
                        , v_control
                        , v_partition_timestamp_start
                        , v_control
                        , v_partition_timestamp_end);
    ELSE
        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (to_timestamp(%I) >= %L AND to_timestamp(%I) < %L)'
                        , v_parent_schema
                        , v_partition_name
                        , v_partition_name||'_partition_check'
                        , v_control
                        , v_partition_timestamp_start
                        , v_control
                        , v_partition_timestamp_end);
    END IF;

    EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I'
                    , v_parent_schema
                    , v_partition_name
                    , v_parent_schema
                    , v_parent_tablename);

    -- If custom time, set extra config options.
    IF v_type = 'time-custom' THEN
        INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
        VALUES ( p_parent_table, v_parent_schema||'.'||v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
    END IF;

    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);

    PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Subpartitioning %s.%s', v_parent_schema, v_partition_name));
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
                , p_epoch := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

    END LOOP; -- end sub partitioning LOOP

    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s. Attempted intervals: %s', p_parent_table, p_partition_times));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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



/*
 * Apply constraints managed by partman extension
 */
CREATE OR REPLACE FUNCTION apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT FALSE, p_job_id bigint DEFAULT NULL, p_debug boolean DEFAULT FALSE) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_child_exists                  text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_constraint_values             record;
v_control                       text;
v_datetime_string               text;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             bigint; 
v_last_partition_timestamp      timestamp;
v_max_id                        bigint;
v_max_timestamp                 timestamp;
v_old_search_path               text;
v_optimize_constraint           int;
v_parent_schema                 text;
v_parent_table                  text;
v_parent_tablename              text;
v_partition_interval            text;
v_partition_suffix              text;
v_premake                       int;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN

SELECT parent_table
    , partition_type
    , control
    , premake
    , partition_interval
    , optimize_constraint
    , datetime_string
    , constraint_cols
    , jobmon
INTO v_parent_table
    , v_type
    , v_control
    , v_premake
    , v_partition_interval
    , v_optimize_constraint
    , v_datetime_string
    , v_constraint_cols
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND constraint_cols IS NOT NULL;

IF v_constraint_cols IS NULL THEN
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

SELECT schemaname, tablename 
INTO v_parent_schema, v_parent_tablename 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(v_parent_table, '.', 1)
AND tablename = split_part(v_parent_table, '.', 2);

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE CONSTRAINT: %s', v_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
END IF;

-- If p_child_table is null, figure out the partition that is the one right before the optimize_constraint value backwards.
IF p_child_table IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying additional constraints: Automatically determining most recent child on which to apply constraints');
    END IF;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_parent_table, 'DESC') LIMIT 1;

    IF v_type IN ('time', 'time-custom') THEN
        SELECT child_start_time INTO v_last_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, v_parent_table);
        v_partition_suffix := to_char(v_last_partition_timestamp - (v_partition_interval::interval * (v_optimize_constraint + v_premake + 1) ), v_datetime_string);
    ELSIF v_type = 'id' THEN
        SELECT child_start_id INTO v_last_partition_id FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, v_parent_table);
        v_partition_suffix := (v_last_partition_id - (v_partition_interval::int * (v_optimize_constraint + v_premake + 1) ))::text; 
    END IF;

    v_child_tablename := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF p_debug THEN
        RAISE NOTICE 'apply_constraint: v_parent_tablename: % , v_partition_suffix: %', v_parent_tablename, v_partition_suffix;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Target child table: %s.%s', v_parent_schema, v_child_tablename));
    END IF;
ELSE
    v_child_tablename = split_part(p_child_table, '.', 2);
END IF;
    
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Applying additional constraints: Checking if target child table exists');
END IF;

SELECT tablename FROM pg_catalog.pg_tables INTO v_child_exists WHERE schemaname = v_parent_schema AND tablename = v_child_tablename;
IF v_child_exists IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', format('Target child table (%s) does not exist. Skipping constraint creation.', v_child_tablename));
        IF p_job_id IS NULL THEN
            PERFORM close_job(v_job_id);
        END IF;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Target child table (%) does not exist. Skipping constraint creation.', v_child_tablename;
    END IF;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOREACH v_col IN ARRAY v_constraint_cols
LOOP
    SELECT con.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint con
    JOIN pg_class c ON c.oid = con.conrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    JOIN pg_catalog.pg_attribute a ON con.conrelid = a.attrelid 
    WHERE c.relname = v_child_tablename
        AND n.nspname = v_parent_schema
        AND con.conname LIKE 'partmanconstr_%'
        AND con.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] OPERATOR(pg_catalog.<@) con.conkey
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Applying new constraint on column: %s', v_col));
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Partman managed constraint already exists on this table (%s) and column (%s). Skipping creation.', v_child_tablename, v_col));
        END IF;
        IF p_debug THEN
            RAISE NOTICE 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_tablename, v_col ;
        END IF;
        CONTINUE;
    END IF;

    -- Ensure column name gets put on end of constraint name to help avoid naming conflicts 
    v_constraint_name := @extschema@.check_name_length('partmanconstr_'||v_child_tablename, p_suffix := '_'||v_col);

    EXECUTE format('SELECT min(%I)::text AS min, max(%I)::text AS max FROM %I.%I', v_col, v_col, v_parent_schema, v_child_tablename) INTO v_constraint_values;

    IF v_constraint_values IS NOT NULL THEN
        v_sql := format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I <= %L)'
                            , v_parent_schema
                            , v_child_tablename
                            , v_constraint_name
                            , v_col
                            , v_constraint_values.min
                            , v_col
                            , v_constraint_values.max);
        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('New constraint created: %s', v_sql));
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Given column (%s) contains all NULLs. No constraint created', v_col));
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Running analyze on partition set: %s', v_parent_table));
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Running analyze on partition set: %', v_parent_table;
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE CONSTRAINT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


/*
 * Check if parent table is a subpartition of an already existing partition set managed by pg_partman
 *  If so, return the limits of what child tables can be created under the given parent table based on its own suffix
 */
CREATE OR REPLACE FUNCTION check_subpartition_limits(p_parent_table text, p_type text, OUT sub_min text, OUT sub_max text) RETURNS record
    LANGUAGE plpgsql
    AS $$
DECLARE

v_datetime_string       text;
v_id_position           int;
v_parent_schema         text;
v_parent_tablename      text;
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

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

-- CTE query is done individually for each type (time, id) because it should return NULL if the top parent is not the same type in a subpartition set (id->time or time->id)

IF p_type = 'id' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_parent_tablename
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'id';

    IF v_top_parent IS NOT NULL THEN 
        SELECT child_start_id::text, child_end_id::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_parent);
    END IF;

ELSIF p_type = 'time' THEN

    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_parent_tablename
    ) SELECT n.nspname||'.'||c.relname, p.datetime_string, p.partition_interval, p.partition_type
      INTO v_top_parent, v_top_datetime_string, v_top_interval, v_top_type
      FROM pg_catalog.pg_class c
      JOIN top_oid t ON c.oid = t.top_parent_oid
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
      JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
      WHERE c.oid = t.top_parent_oid
      AND p.partition_type = 'time' OR p.partition_type = 'time-custom';

    IF v_top_parent IS NOT NULL THEN 
        SELECT child_start_time::text, child_end_time::text 
        INTO sub_min, sub_max
        FROM @extschema@.show_partition_info(p_parent_table, v_top_interval, v_top_parent);
    END IF;

ELSE
    RAISE EXCEPTION 'Invalid type given as parameter to check_subpartition_limits()';
END IF;

RETURN;

END
$$;


/*
 * Function to turn a table into the parent of a partition set
 */
CREATE OR REPLACE FUNCTION create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_use_run_maintenance boolean DEFAULT NULL
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch boolean DEFAULT false
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_base_timestamp                timestamp;
v_count                         int := 1;
v_datetime_string               text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_interval                   bigint;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_created        boolean;
v_max                           bigint;
v_notnull                       boolean;
v_old_search_path               text;
v_parent_partition_id           bigint;
v_parent_partition_timestamp    timestamp;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_time                timestamp;
v_partition_time_array          timestamp[];
v_partition_id_array            bigint[];
v_row                           record;
v_run_maint                     boolean;
v_sql                           text;
v_start_time                    timestamp;
v_starting_partition_id         bigint;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_parent                    text;
v_success                       boolean := false;
v_time_interval                 interval;
v_top_datetime_string           text;
v_top_parent_schema             text := split_part(p_parent_table, '.', 1);
v_top_parent_table              text := split_part(p_parent_table, '.', 2);

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull 
FROM pg_catalog.pg_attribute a
JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE c.relname = v_parent_tablename
AND n.nspname = v_parent_schema
AND a.attname = p_control;
    IF v_notnull = false OR v_notnull IS NULL THEN
        RAISE EXCEPTION 'Control column given (%) for parent table (%) does not exist or must be set to NOT NULL', p_control, p_parent_table;
    END IF;

IF p_type = 'id' AND p_epoch = true THEN
    RAISE EXCEPTION 'p_epoch can only be used with time-based partitioning';
END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF p_use_run_maintenance IS NOT NULL THEN
    IF p_use_run_maintenance IS FALSE AND (p_type = 'time' OR p_type = 'time-custom') THEN
        RAISE EXCEPTION 'p_run_maintenance cannot be set to false for time based partitioning';
    END IF;
    v_run_maint := p_use_run_maintenance;
ELSIF p_type = 'time' OR p_type = 'time-custom' THEN
    v_run_maint := TRUE;
ELSIF p_type = 'id' THEN
    v_run_maint := FALSE;
ELSE
    RAISE EXCEPTION 'use_run_maintenance value cannot be set NULL';
END IF;

EXECUTE format('LOCK TABLE %I.%I IN ACCESS EXCLUSIVE MODE', v_parent_schema, v_parent_tablename);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN SETUP PARENT: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Creating initial partitions on new parent table: %s', p_parent_table));
END IF;

-- If this parent table has siblings that are also partitioned (subpartitions), ensure this parent gets added to part_config_sub table so future maintenance will subpartition it
-- Just doing in a loop to avoid having to assign a bunch of variables (should only run once, if at all; constraint should enforce only one value.)
FOR v_row IN 
    WITH parent_table AS (
        SELECT h.inhparent AS parent_oid
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON h.inhrelid = c.oid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename
        AND n.nspname = v_parent_schema
    ), sibling_children AS (
        SELECT i.inhrelid::regclass::text AS tablename 
        FROM pg_inherits i
        JOIN parent_table p ON i.inhparent = p.parent_oid
    )
    SELECT DISTINCT sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_use_run_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
    FROM @extschema@.part_config_sub a
    JOIN sibling_children b on a.sub_parent = b.tablename LIMIT 1
LOOP
    INSERT INTO @extschema@.part_config_sub (
        sub_parent
        , sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_use_run_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon)
    VALUES (
        p_parent_table
        , v_row.sub_partition_type
        , v_row.sub_control
        , v_row.sub_partition_interval
        , v_row.sub_constraint_cols
        , v_row.sub_premake
        , v_row.sub_inherit_fk
        , v_row.sub_retention
        , v_row.sub_retention_schema
        , v_row.sub_retention_keep_table
        , v_row.sub_retention_keep_index
        , v_row.sub_use_run_maintenance
        , v_row.sub_epoch
        , v_row.sub_optimize_trigger
        , v_row.sub_optimize_constraint
        , v_row.sub_infinite_time_partitions
        , v_row.sub_jobmon);
END LOOP;

IF p_type = 'time' OR p_type = 'time-custom' THEN

    CASE
        WHEN p_interval = 'yearly' THEN
            v_time_interval := '1 year';
        WHEN p_interval = 'quarterly' THEN
            v_time_interval := '3 months';
        WHEN p_interval = 'monthly' THEN
            v_time_interval := '1 month';
        WHEN p_interval  = 'weekly' THEN
            v_time_interval := '1 week';
        WHEN p_interval = 'daily' THEN
            v_time_interval := '1 day';
        WHEN p_interval = 'hourly' THEN
            v_time_interval := '1 hour';
        WHEN p_interval = 'half-hour' THEN
            v_time_interval := '30 mins';
        WHEN p_interval = 'quarter-hour' THEN
            v_time_interval := '15 mins';
        ELSE
            IF p_type <> 'time-custom' THEN
                RAISE EXCEPTION 'Must use a predefined time interval if not using type "time-custom". See documentation.';
            END IF;
            v_time_interval := p_interval::interval;
            IF v_time_interval < '1 second'::interval THEN
                RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
            END IF;
    END CASE;

    -- First partition is either the min premake or p_start_partition
    v_start_time := COALESCE(p_start_partition::timestamp, CURRENT_TIMESTAMP - (v_time_interval * p_premake));

    IF v_time_interval >= '1 year' THEN
        v_base_timestamp := date_trunc('year', v_start_time);
        IF v_time_interval >= '10 years' THEN
            v_base_timestamp := date_trunc('decade', v_start_time);
            IF v_time_interval >= '100 years' THEN
                v_base_timestamp := date_trunc('century', v_start_time);
                IF v_time_interval >= '1000 years' THEN
                    v_base_timestamp := date_trunc('millennium', v_start_time);
                END IF; -- 1000
            END IF; -- 100
        END IF; -- 10
    END IF; -- 1

    v_datetime_string := 'YYYY';
    IF v_time_interval < '1 year' THEN
        IF p_interval = 'quarterly' THEN
            v_base_timestamp := date_trunc('quarter', v_start_time);
            v_datetime_string = 'YYYY"q"Q';
        ELSE
            v_base_timestamp := date_trunc('month', v_start_time); 
            v_datetime_string := v_datetime_string || '_MM';
        END IF;
        IF v_time_interval < '1 month' THEN
            IF p_interval = 'weekly' THEN
                v_base_timestamp := date_trunc('week', v_start_time);
                v_datetime_string := 'IYYY"w"IW';
            ELSE 
                v_base_timestamp := date_trunc('day', v_start_time);
                v_datetime_string := v_datetime_string || '_DD';
            END IF;
            IF v_time_interval < '1 day' THEN
                v_base_timestamp := date_trunc('hour', v_start_time);
                v_datetime_string := v_datetime_string || '_HH24MI';
                IF v_time_interval < '1 minute' THEN
                    v_base_timestamp := date_trunc('minute', v_start_time);
                    v_datetime_string := v_datetime_string || 'SS';
                END IF; -- minute
            END IF; -- day
        END IF; -- month
    END IF; -- year

    v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
    LOOP
        -- If current loop value is less than or equal to the value of the max premake, add time to array.
        IF (v_base_timestamp + (v_time_interval * v_count)) < (CURRENT_TIMESTAMP + (v_time_interval * p_premake)) THEN
            BEGIN
                v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamp;
                v_partition_time_array := array_append(v_partition_time_array, v_partition_time);
            EXCEPTION WHEN datetime_field_overflow THEN
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                    Child partition creation after time % skipped', v_partition_time;
                v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_partition_time||' skipped');
                CONTINUE;
            END;
        ELSE
            EXIT; -- all needed partitions added to array. Exit the loop.
        END IF;
        v_count := v_count + 1;
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , epoch
        , control
        , premake
        , constraint_cols
        , datetime_string
        , use_run_maintenance
        , inherit_fk
        , jobmon) 
    VALUES (
        p_parent_table
        , p_type
        , v_time_interval
        , p_epoch
        , p_control
        , p_premake
        , p_constraint_cols
        , v_datetime_string
        , v_run_maint
        , p_inherit_fk
        , p_jobmon);
    v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);

    IF v_last_partition_created = false THEN 
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- First see if this parent is a subpartition managed by pg_partman
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename
            AND n.nspname = v_parent_schema
        ) SELECT n.nspname, c.relname 
        INTO v_top_parent_schema, v_top_parent_table 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;
        IF v_top_parent_table IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            SELECT child_start_time INTO v_parent_partition_timestamp FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_schema||'.'||v_top_parent_table);
            IF v_base_timestamp >= v_parent_partition_timestamp THEN
                WHILE v_base_timestamp >= v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp - v_time_interval;
                END LOOP;
                v_base_timestamp := v_base_timestamp + v_time_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_base_timestamp < v_parent_partition_timestamp THEN
                WHILE v_base_timestamp < v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp + v_time_interval;
                END LOOP;
                -- Don't need to remove one since new starting time will fit in top parent interval
            END IF;
            v_partition_time_array := NULL;
            v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
            v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE EXCEPTION 'No child tables created. Unexpected edge case encountered. Please report this error to author with conditions that led to it.';
        END IF; 
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Time partitions premade: %s', p_premake));
    END IF;
END IF;

IF p_type = 'id' THEN
    v_id_interval := p_interval::bigint;
    IF v_id_interval < 10 THEN
        RAISE EXCEPTION 'Interval for serial partitioning must be greater than or equal to 10';
    END IF;

    -- Check if parent table is a subpartition of an already existing id partition set managed by pg_partman. 
    WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE n.nspname = v_higher_parent_schema
            AND c.relname = v_higher_parent_table
        ) SELECT n.nspname, c.relname
        INTO v_higher_parent_schema, v_higher_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.partition_type = 'id';

        IF v_higher_parent_table IS NOT NULL THEN
            -- v_top_parent initially set in DECLARE
            v_top_parent_schema := v_higher_parent_schema;
            v_top_parent_table := v_higher_parent_table;
        END IF;
    END LOOP;

    -- If custom start partition is set, use that.
    -- If custom start is not set and there is already data, start partitioning with the highest current value and ensure it's grabbed from highest top parent table
    IF p_start_partition IS NOT NULL THEN
        v_max := p_start_partition::bigint;
    ELSE
        v_sql := format('SELECT COALESCE(max(%I)::bigint, 0) FROM %I.%I LIMIT 1'
                    , p_control
                    , v_top_parent_schema
                    , v_top_parent_table);
        EXECUTE v_sql INTO v_max;
    END IF;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 AND 
            (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id_array = array_append(v_partition_id_array, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id_array = array_append(v_partition_id_array, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , control
        , premake
        , constraint_cols
        , use_run_maintenance
        , inherit_fk
        , jobmon) 
    VALUES (
        p_parent_table
        , p_type
        , v_id_interval
        , p_control
        , p_premake
        , p_constraint_cols
        , v_run_maint
        , p_inherit_fk
        , p_jobmon);
    v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
    IF v_last_partition_created = false THEN
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- See if it's actually a subpartition of a parent id partition
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename
            AND n.nspname = v_parent_schema
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.partition_type = 'id';
        IF v_top_parent_table IS NOT NULL THEN
            -- Create the lowest possible partition that is within the boundary of the parent
             SELECT child_start_id INTO v_parent_partition_id FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_table);
            IF v_starting_partition_id >= v_parent_partition_id THEN
                WHILE v_starting_partition_id >= v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id - v_id_interval;
                END LOOP;
                v_starting_partition_id := v_starting_partition_id + v_id_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_starting_partition_id < v_parent_partition_id THEN
                WHILE v_starting_partition_id < v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id + v_id_interval;
                END LOOP;
                -- Don't need to remove one since new starting id will fit in top parent interval
            END IF;
            v_partition_id_array = NULL;
            v_partition_id_array = array_append(v_partition_id_array, v_starting_partition_id);
            v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE EXCEPTION 'No child tables created. Unexpected edge case encountered. Please report this error to author with conditions that led to it.';
        END IF;
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;
IF p_type = 'time' OR p_type = 'time-custom' THEN
    PERFORM @extschema@.create_function_time(p_parent_table, v_job_id);
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id' THEN
    PERFORM @extschema@.create_function_id(p_parent_table, v_job_id);  
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID function created');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition trigger');
END IF;
PERFORM @extschema@.create_trigger(p_parent_table);

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

v_success := true;

RETURN v_success;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE PARENT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition creation for table '||p_parent_table||' failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
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


/*
 * Function to drop child tables from an id-based partition set. 
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */
CREATE OR REPLACE FUNCTION drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_control                   text;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_max                       bigint;
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        bigint;
v_partition_id              bigint;
v_retention                 bigint;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_row_max_id                record;
v_step_id                   bigint;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_id'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_id already running.';
    RETURN 0;
END IF;

-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        partition_interval::bigint
        , control
        , retention::bigint
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_partition_interval
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND partition_type = 'id'
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
     SELECT  
        partition_interval::bigint
        , control
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_partition_interval
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND partition_type = 'id'; 
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
        EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max_id.partition_schemaname, v_row_max_id.partition_tablename) INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;

-- Loop through child tables of the given parent
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
     SELECT child_start_id INTO v_partition_id FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_partition_interval)) THEN
        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP ID PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Uninherit table %s.%s from %s', v_row.partition_schemaname, v_row.partition_tablename, p_parent_table));
        END IF;
        EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
            , v_row.partition_schemaname
            , v_row.partition_tablename
            , v_parent_schema
            , v_parent_tablename);
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                FOR v_index IN 
                     WITH child_info AS (
                        SELECT c1.oid
                        FROM pg_catalog.pg_class c1
                        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                        WHERE c1.relname = v_row.partition_tablename
                        AND n1.nspname = v_row.partition_schema
                    )
                    SELECT c.relname as name
                        , con.conname
                    FROM pg_catalog.pg_index i
                    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                    LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                    JOIN child_info ON i.indrelid = child_info.oid
                LOOP
                    IF v_jobmon_schema IS NOT NULL THEN
                        v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                            , v_index.name
                            , v_row.partition_schemaname
                            , v_row.partition_tablename));
                    END IF;
                    IF v_index.conname IS NOT NULL THEN
                        EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I', v_row.partition_schemaname, v_row.partition_tablename, v_index.conname);
                    ELSE
                        EXECUTE format('DROP INDEX %I.%I', v_row.partition_schemaname, v_index.name);
                    END IF;
                    IF v_jobmon_schema IS NOT NULL THEN
                        PERFORM update_step(v_step_id, 'OK', 'Done');
                    END IF;
                END LOOP;
            END IF;
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                        , v_row.partition_schemaname
                                                        , v_row.partition_tablename
                                                        , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_retention_schema);

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname ||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP ID PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


/*
 * Function to drop child tables from a time-based partition set.
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */
CREATE OR REPLACE FUNCTION drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_datetime_string           text;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        interval;
v_partition_timestamp       timestamp;
v_retention                 interval;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_step_id                   bigint;
v_type                      text;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_time'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_time already running.';
    RETURN 0;
END IF;

-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        partition_type
        , partition_interval::interval
        , retention::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_type
        , v_partition_interval
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (partition_type = 'time' OR partition_type = 'time-custom')
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
    SELECT  
        partition_type
        , partition_interval::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_type
        , v_partition_interval
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (partition_type = 'time' OR partition_type = 'time-custom'); 
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

-- Loop through child tables of the given parent
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
    -- pull out datetime portion of partition's tablename to make the next one
     SELECT child_start_time INTO v_partition_timestamp FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention < (CURRENT_TIMESTAMP - (v_partition_timestamp + v_partition_interval)) THEN
        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP TIME PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Uninherit table %s.%s from %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , p_parent_table));
        END IF;
        EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                , v_row.partition_schemaname
                , v_row.partition_tablename
                , v_parent_schema
                , v_parent_tablename);
        IF v_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                FOR v_index IN 
                    WITH child_info AS (
                        SELECT c1.oid
                        FROM pg_catalog.pg_class c1
                        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                        WHERE c1.relname = v_row.partition_tablename
                        AND n1.nspname = v_row.partition_schemaname
                    )
                    SELECT c.relname as name
                        , con.conname
                    FROM pg_catalog.pg_index i
                    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                    LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                    JOIN child_info ON i.indrelid = child_info.oid
                LOOP
                    IF v_jobmon_schema IS NOT NULL THEN
                        v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                                                            , v_index.name
                                                            , v_row.partition_schemaname
                                                            , v_row.partition_tablename));
                    END IF;
                    IF v_index.conname IS NOT NULL THEN
                        EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I'
                                        , v_row.partition_schemaname
                                        , v_row.partition_tablename
                                        , v_index.conname);
                    ELSE
                        EXECUTE format('DROP INDEX %I.%I', v_parent_schema, v_index.name);
                    END IF;
                    IF v_jobmon_schema IS NOT NULL THEN
                        PERFORM update_step(v_step_id, 'OK', 'Done');
                    END IF;
                END LOOP;
            END IF;
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I', v_row.partition_schemaname, v_row.partition_tablename, v_retention_schema);


            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP TIME PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE OR REPLACE FUNCTION partition_data_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_lock_wait numeric DEFAULT 0, p_order text DEFAULT 'ASC') RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_current_partition_name    text;
v_epoch                     boolean;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_timestamp   timestamp;
v_min_partition_timestamp   timestamp;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        interval;
v_partition_suffix          text;
v_partition_timestamp       timestamp[];
v_quarter                   text;
v_rowcount                  bigint;
v_sql                       text;
v_start_control             timestamp;
v_time_position             int;
v_total_rows                bigint := 0;
v_type                      text;
v_year                      text;

BEGIN

SELECT partition_type
    , partition_interval::interval
    , control
    , datetime_string
    , epoch
INTO v_type
    , v_partition_interval
    , v_control
    , v_datetime_string
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (partition_type = 'time' OR partition_type = 'time-custom');
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

FOR i IN 1..p_batch_count LOOP

    IF v_epoch = false THEN
        IF p_order = 'ASC' THEN
            EXECUTE format('SELECT min(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSIF p_order = 'DESC' THEN
            EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSE
            RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
        END IF;
    ELSE
        IF p_order = 'ASC' THEN
            EXECUTE format('SELECT to_timestamp(min(%I)) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSIF p_order = 'DESC' THEN
            EXECUTE format('SELECT to_timestamp(max(%I)) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSE
            RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
        END IF;
    END IF;

    IF v_start_control IS NULL THEN
        EXIT;
    END IF;

    IF v_type = 'time' THEN
        CASE
            WHEN v_partition_interval = '15 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '15min'::interval * floor(date_part('minute', v_start_control) / 15.0);
            WHEN v_partition_interval = '30 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '30min'::interval * floor(date_part('minute', v_start_control) / 30.0);
            WHEN v_partition_interval = '1 hour' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control);
            WHEN v_partition_interval = '1 day' THEN
                v_min_partition_timestamp := date_trunc('day', v_start_control);
            WHEN v_partition_interval = '1 week' THEN
                v_min_partition_timestamp := date_trunc('week', v_start_control);
            WHEN v_partition_interval = '1 month' THEN
                v_min_partition_timestamp := date_trunc('month', v_start_control);
            WHEN v_partition_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_start_control);
            WHEN v_partition_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_start_control);
        END CASE;
    ELSIF v_type = 'time-custom' THEN
        SELECT child_start_time INTO v_min_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition
            , v_partition_interval
            , p_parent_table);
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        LOOP
            IF v_start_control >= v_min_partition_timestamp AND v_start_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                BEGIN
                    IF v_start_control > v_max_partition_timestamp THEN
                        -- Keep going forward in time, checking if child partition time interval encompasses the current v_start_control value
                        v_min_partition_timestamp := v_max_partition_timestamp;
                        v_max_partition_timestamp := v_max_partition_timestamp + v_partition_interval;

                    ELSE
                        -- Keep going backwards in time, checking if child partition time interval encompasses the current v_start_control value
                        v_max_partition_timestamp := v_min_partition_timestamp;
                        v_min_partition_timestamp := v_min_partition_timestamp - v_partition_interval;
                    END IF;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_interval;
                END;
            END IF;
        END LOOP;

    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF p_order = 'ASC' THEN
        -- Ensure batch interval given as parameter doesn't cause maximum to overflow the current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_timestamp + v_partition_interval) THEN
            v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        ELSE
            v_max_partition_timestamp := v_start_control + p_batch_interval;
        END IF;
    ELSIF p_order = 'DESC' THEN
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        -- Ensure batch interval given as parameter doesn't cause minimum to underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_timestamp THEN
            v_min_partition_timestamp = v_start_control - p_batch_interval;
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
                IF v_epoch = false THEN
                    v_sql := format('SELECT * FROM ONLY %I.%I WHERE %I >= %L AND %I < %L FOR UPDATE NOWAIT'
                                        , v_parent_schema
                                        , v_parent_tablename
                                        , v_control
                                        , v_min_partition_timestamp
                                        , v_control
                                        , v_max_partition_timestamp);
                ELSE
                    v_sql := format('SELECT * FROM ONLY %I.%I WHERE to_timestamp(%I) >= %L AND to_timestamp(%I) < %L FOR UPDATE NOWAIT'
                                        , v_parent_schema
                                        , v_parent_tablename
                                        , v_control
                                        , v_min_partition_timestamp
                                        , v_control
                                        , v_max_partition_timestamp);
                END IF;
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

    PERFORM @extschema@.create_partition_time(p_parent_table, v_partition_timestamp);
    -- This suffix generation code is in create_partition_time() as well
    v_partition_suffix := to_char(v_min_partition_timestamp, v_datetime_string);
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF v_epoch = false THEN
        v_sql := format('WITH partition_data AS (
                            DELETE FROM ONLY %I.%I WHERE %I >= %L AND %I < %L RETURNING *)
                         INSERT INTO %I.%I SELECT * FROM partition_data'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_control
                            , v_min_partition_timestamp
                            , v_control
                            , v_max_partition_timestamp
                            , v_parent_schema
                            , v_current_partition_name);
    ELSE
        v_sql := format('WITH partition_data AS (
                            DELETE FROM ONLY %I.%I WHERE to_timestamp(%I) >= %L AND to_timestamp(%I) < %L RETURNING *)
                         INSERT INTO %I.%I SELECT * FROM partition_data'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_control
                            , v_min_partition_timestamp
                            , v_control
                            , v_max_partition_timestamp
                            , v_parent_schema
                            , v_current_partition_name);
    END IF;
    EXECUTE v_sql;
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

PERFORM @extschema@.create_function_time(p_parent_table);

RETURN v_total_rows;

END
$$;


/*
 * Given a parent table and partition value, return the name of the child partition it would go in.
 * If using epoch time partitioning, give the text representation of the timestamp NOT the epoch integer value (use to_timestamp() to convert epoch values).
 * Also returns just the suffix value and true if the child table exists or false if it does not
 */
CREATE OR REPLACE FUNCTION show_partition_name(p_parent_table text, p_value text, OUT partition_table text, OUT suffix_timestamp timestamp, OUT suffix_id bigint, OUT table_exists boolean) RETURNS record
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_child_exists          text;
v_datetime_string       text;
v_max_range             timestamptz;
v_min_range             timestamptz;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    text;
v_type                  text;

BEGIN

SELECT partition_type 
    , partition_interval
    , datetime_string
INTO v_type
    , v_partition_interval
    , v_datetime_string 
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;

IF v_type IS NULL THEN
    RAISE EXCEPTION 'Parent table given is not managed by pg_partman (%)', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)
AND tablename = split_part(p_parent_table, '.', 2);
IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Parent table given does not exist (%)', p_parent_table;
END IF;

IF v_type = 'time' THEN
    CASE
        WHEN v_partition_interval::interval = '15 mins' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp) + 
                '15min'::interval * floor(date_part('minute', p_value::timestamp) / 15.0);
        WHEN v_partition_interval::interval = '30 mins' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp) + 
                '30min'::interval * floor(date_part('minute', p_value::timestamp) / 30.0);
        WHEN v_partition_interval::interval = '1 hour' THEN
            suffix_timestamp := date_trunc('hour', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 day' THEN
            suffix_timestamp := date_trunc('day', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 week' THEN
            suffix_timestamp := date_trunc('week', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 month' THEN
            suffix_timestamp := date_trunc('month', p_value::timestamp);
        WHEN v_partition_interval::interval = '3 months' THEN
            suffix_timestamp := date_trunc('quarter', p_value::timestamp);
        WHEN v_partition_interval::interval = '1 year' THEN
            suffix_timestamp := date_trunc('year', p_value::timestamp);
    END CASE;
    partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, to_char(suffix_timestamp, v_datetime_string), TRUE);
ELSIF v_type = 'id' THEN
    suffix_id := (p_value::bigint - (p_value::bigint % v_partition_interval::bigint));
    partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, suffix_id::text, TRUE);
ELSIF v_type = 'time-custom' THEN

    SELECT child_table, lower(partition_range) INTO partition_table, suffix_timestamp FROM @extschema@.custom_time_partitions 
        WHERE parent_table = p_parent_table AND partition_range @> p_value::timestamptz;

    IF partition_table IS NULL THEN
        SELECT max(upper(partition_range)) INTO v_max_range FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table;
        SELECT min(lower(partition_range)) INTO v_min_range FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table;
        IF p_value::timestamp >= v_max_range THEN
            suffix_timestamp := v_max_range;
            LOOP
                -- Keep incrementing higher until given value is below the upper range
                suffix_timestamp := suffix_timestamp + v_partition_interval::interval;
                IF p_value::timestamp < suffix_timestamp THEN
                    -- Have to subtract one interval because the value would actually be in the partition previous 
                    --      to this partition timestamp since the partition names contain the lower boundary
                    suffix_timestamp := suffix_timestamp - v_partition_interval::interval;
                    EXIT;
                END IF;
            END LOOP;
        ELSIF p_value::timestamp < v_min_range THEN
            suffix_timestamp := v_min_range;
            LOOP
                -- Keep decrementing lower until given value is below or equal to the lower range
                suffix_timestamp := suffix_timestamp - v_partition_interval::interval;
                IF p_value::timestamp >= suffix_timestamp THEN
                    EXIT;
                END IF;
            END LOOP;
        ELSE
            RAISE EXCEPTION 'Unable to determine a valid child table for the given parent table and value';
        END IF;

        partition_table := v_parent_schema||'.'||@extschema@.check_name_length(v_parent_tablename, to_char(suffix_timestamp, v_datetime_string), TRUE);
    END IF;
END IF;

SELECT tablename INTO v_child_exists
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(partition_table, '.', 1)
AND tablename = split_part(partition_table, '.', 2);

IF v_child_exists IS NOT NULL THEN
    table_exists := true;
ELSE
    table_exists := false;
END IF;

RETURN;

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
