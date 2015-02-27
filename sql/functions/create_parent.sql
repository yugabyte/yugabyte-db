/*
 * Function to turn a table into the parent of a partition set
 */
CREATE FUNCTION create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_use_run_maintenance boolean DEFAULT NULL
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_base_timestamp                timestamp;
v_count                         int := 1;
v_datetime_string               text;
v_higher_parent                 text := p_parent_table;
v_id_interval                   bigint;
v_id_position                   int;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_created        boolean;
v_max                           bigint;
v_notnull                       boolean;
v_old_search_path               text;
v_parent_partition_id           bigint;
v_parent_partition_timestamp    timestamp;
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
v_tablename                     text;
v_time_interval                 interval;
v_time_position                 int;
v_top_datetime_string           text;
v_top_parent                    text := p_parent_table;

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull FROM pg_attribute WHERE attrelid = p_parent_table::regclass AND attname = p_control;
    IF v_notnull = false OR v_notnull IS NULL THEN
        RAISE EXCEPTION 'Control column (%) for parent table (%) must be NOT NULL', p_control, p_parent_table;
    END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

IF p_type = 'time-custom' AND @extschema@.check_version('9.2.0') IS FALSE THEN
    RAISE EXCEPTION 'The "time-custom" type requires a minimum PostgreSQL version of 9.2.0';
END IF;

EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF p_use_run_maintenance IS NOT NULL THEN
    IF p_use_run_maintenance IS FALSE AND (p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom') THEN
        RAISE EXCEPTION 'p_run_maintenance cannot be set to false for time based partitioning';
    END IF;
    v_run_maint := p_use_run_maintenance;
ELSIF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN
    v_run_maint := TRUE;
ELSIF p_type = 'id-static' OR p_type ='id-dynamic' THEN
    v_run_maint := FALSE;
ELSE
    RAISE EXCEPTION 'use_run_maintenance value cannot be set NULL';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN SETUP PARENT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating initial partitions on new parent table: '||p_parent_table);
END IF;

-- If this parent table has siblings that are also partitioned (subpartitions), ensure it gets added to part_config_sub table so future maintenance will subpartition it
-- Just doing in a loop to avoid having to assign a bunch of variables (should only run once, if at all; constraint should enforce only one value.)
FOR v_row IN 
    WITH parent_table AS (
        SELECT h.inhparent as parent_oid
        from pg_inherits h
        where h.inhrelid::regclass = p_parent_table::regclass
    ), sibling_children as (
        select i.inhrelid::regclass::text as tablename 
        from pg_inherits i
        join parent_table p on i.inhparent = p.parent_oid
    )
    SELECT DISTINCT sub_type
        , sub_control
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
    FROM @extschema@.part_config_sub a
    JOIN sibling_children b on a.sub_parent = b.tablename LIMIT 1
LOOP
    INSERT INTO @extschema@.part_config_sub (
        sub_parent
        , sub_type
        , sub_control
        , sub_part_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_use_run_maintenance
        , sub_jobmon)
    VALUES (
        p_parent_table
        , v_row.sub_type
        , v_row.sub_control
        , v_row.sub_part_interval
        , v_row.sub_constraint_cols
        , v_row.sub_premake
        , v_row.sub_inherit_fk
        , v_row.sub_retention
        , v_row.sub_retention_schema
        , v_row.sub_retention_keep_table
        , v_row.sub_retention_keep_index
        , v_row.sub_use_run_maintenance
        , v_row.sub_jobmon);
END LOOP;

IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN

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
        , type
        , part_interval
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
            WHERE n.nspname||'.'||c.relname = p_parent_table 
        ) SELECT n.nspname||'.'||c.relname, p.datetime_string
        INTO v_top_parent, v_top_datetime_string
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;
        IF v_top_parent IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            v_time_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
            v_parent_partition_timestamp := to_timestamp(substring(p_parent_table from v_time_position), v_top_datetime_string);
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
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    v_id_interval := p_interval::bigint;
    IF v_id_interval < 10 THEN
        RAISE EXCEPTION 'Interval for serial partitioning must be greater than or equal to 10';
    END IF;

    -- Check if parent table is a subpartition of an already existing id partition set managed by pg_partman. 
    WHILE v_higher_parent IS NOT NULL LOOP -- initially set in DECLARE
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE n.nspname||'.'||c.relname = v_higher_parent
        ) SELECT n.nspname||'.'||c.relname
        INTO v_higher_parent
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.type = 'id-static' OR p.type = 'id-dynamic';

        IF v_higher_parent IS NOT NULL THEN
            -- v_top_parent initially set in DECLARE
            v_top_parent := v_higher_parent;
        END IF;
    END LOOP;

    -- If custom start partition is set, use that.
    -- If custom start is not set and there is already data, start partitioning with the highest current value and ensure it's grabbed from highest top parent table
    v_sql := 'SELECT COALESCE('||quote_nullable(p_start_partition::bigint)||', max('||p_control||')::bigint, 0) FROM '||v_top_parent||' LIMIT 1';
    EXECUTE v_sql INTO v_max;
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
        , type
        , part_interval
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
            WHERE n.nspname||'.'||c.relname = p_parent_table 
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.type = 'id-static' OR p.type = 'id-dynamic';
        IF v_top_parent IS NOT NULL THEN
            -- Create the lowest possible partition that is within the boundary of the parent
            v_id_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
            v_parent_partition_id = substring(p_parent_table from v_id_position)::bigint;
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
IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN
    PERFORM @extschema@.create_function_time(p_parent_table);
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    PERFORM @extschema@.create_function_id(p_parent_table);  
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
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

v_success := true;

RETURN v_success;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE PARENT: '||p_parent_table||''')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition creation for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


