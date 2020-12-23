-- Fixed a bug in sub-partitioning that would cause child tables outside of the time boundaries of the parent partitions to be created when using time->time sub-partitioning. A user encountered the error when doing weekly->daily subpartitioning, but it was possible it could have happened in other interval combinations I had not tested as well (Github Issue #47).
-- Updated reapply_indexes.py script to, by default, only add new indexes and drop ones that don't exist on the parent. Previously it would drop all indexes on all children and recreate them to match the parent. Now it only does the minimal amount of work to make the children match the parent. An additional option (--recreate_all/-R) was added to allow the old behavior of redoing all indexes from scratch if desired (Github Issue #41)
-- Changed the minimal interval that serial partitioning can be done to 10. Ran into issues with an interval of 2 and partitioning anything this low is unrealistic and provides no benefit. 10 seems like a reasonable minimal to have at this point to avoid any future issues. This does not affect any existing partition sets, only newly created ones (Github Issue #39).
-- Serial partition maintenance when using run_maintenance() is now much more efficient. Should run significantly faster for very large partition sets.
-- Fixed bug in subpartition creation when using "time-custom" partitioning type. May have created subpartition child tables that were outside the time boundaries of the parent partitions.
-- Fixed bug in additional constraint management when using "time-custom" partitioning type. May not have always added additional constraints on old child tables.
-- Added constraint on part_config_sub to ensure valid partition types.

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_type_check
CHECK (@extschema@.check_partition_type(sub_type));

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


/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE OR REPLACE FUNCTION create_function_id(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_function_name                 text;
v_higher_parent                 text := p_parent_table;
v_id_position                   int;
v_job_id                        bigint;
v_jobmon                        text;
v_jobmon_schema                 text;
v_last_partition                text;
v_max                           bigint;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_row_max_id                    record;
v_run_maint                     boolean;
v_step_id                       bigint;
v_top_parent                    text := p_parent_table;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
    , premake
    , use_run_maintenance
    , jobmon
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_run_maint
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT show_partitions INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

IF v_type = 'id-static' THEN
    -- Get the highest level top parent if multi-level partitioned in order to get proper max() value below
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
            -- initially set in DECLARE
            v_top_parent := v_higher_parent;
        END IF;

    END LOOP;

    -- Loop through child tables starting from highest to get current max value in partition set
    -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
    FOR v_row_max_id IN
        SELECT show_partitions FROM @extschema@.show_partitions(v_top_parent, 'DESC')
    LOOP
            EXECUTE 'SELECT max('||v_control||') FROM '||v_row_max_id.show_partitions INTO v_max;
            IF v_max IS NOT NULL THEN
                EXIT;
            END IF;
    END LOOP;
    IF v_max IS NULL THEN
        v_max := 0;
    END IF;
    v_current_partition_id = v_max - (v_max % v_part_interval);
    v_next_partition_id := v_current_partition_id + v_part_interval;
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_current_partition_id::text, TRUE);

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id  bigint;
            v_last_partition        text := '||quote_literal(v_last_partition)||';
            v_id_position           int;
            v_next_partition_id     bigint;
            v_next_partition_name   text;         
            v_partition_created     boolean;
        BEGIN
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||v_current_partition_id||' AND NEW.'||v_control||' < '||v_next_partition_id|| ' THEN ';
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_current_partition_name;
            IF v_count > 0 THEN
                v_trig_func := v_trig_func || ' 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';
            ELSE
                v_trig_func := v_trig_func || '
                -- Child table for current values does not exist in this partition set, so write to parent
                RETURN NEW;';
            END IF;

        FOR i IN 1..v_premake LOOP
            v_prev_partition_id := v_current_partition_id - (v_part_interval * i);
            v_next_partition_id := v_current_partition_id + (v_part_interval * i);
            v_final_partition_id := v_next_partition_id + v_part_interval;
            v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_prev_partition_id::text, TRUE);
            v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_next_partition_id::text, TRUE);
            
            -- Check that child table exist before making a rule to insert to them.
            -- Handles edge case of changing premake immediately after running create_parent(). 
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_prev_partition_name;
            IF v_count > 0 THEN
                -- Only handle previous partitions if they're starting above zero
                IF v_prev_partition_id >= 0 THEN
                    v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_prev_partition_id + v_part_interval|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); ';
                END IF;
            END IF;
            
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_next_partition_name;
            IF v_count > 0 THEN
                v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_next_partition_id||' AND NEW.'||v_control||' < '||v_final_partition_id|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*);'; 
            END IF;
        END LOOP;
        v_trig_func := v_trig_func ||'
            ELSE
                RETURN NEW;
            END IF;';

        IF v_run_maint IS FALSE THEN
            v_trig_func := v_trig_func ||'
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_next_partition_id := (substring(v_last_partition from v_id_position)::bigint) + '||v_part_interval||';
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_partition_created := @extschema@.create_partition_id('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    IF v_partition_created THEN
                        PERFORM @extschema@.create_function_id('||quote_literal(p_parent_table)||');
                        PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;';
        END IF;

        v_trig_func := v_trig_func ||'
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current id interval: '||v_current_partition_id||' to '||v_final_partition_id-1);
    END IF;

ELSIF v_type = 'id-dynamic' THEN
    -- The return inside the partition creation check is there to keep really high ID values from creating new partitions.
    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := '||quote_literal(v_last_partition)||';
            v_last_partition_id         bigint;
            v_next_partition_id         bigint;
            v_partition_created         boolean;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := @extschema@.check_name_length('''||v_parent_tablename||''', '''||v_parent_schema||''', v_current_partition_id::text, TRUE);
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_current_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;';

       IF v_run_maint IS FALSE THEN
            v_trig_func := v_trig_func ||'
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF NEW.'||v_control||' >= v_next_partition_id THEN
                    RETURN NEW;
                END IF;
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_partition_created := @extschema@.create_partition_id('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    IF v_partition_created THEN
                        PERFORM @extschema@.create_function_id('||quote_literal(p_parent_table)||');
                        PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;';
        END IF;

        v_trig_func := v_trig_func ||'
        END IF;
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic id table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid id partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE FUNCTION: '||p_parent_table||''')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition function maintenance for table '||p_parent_table||' failed'')' INTO v_step_id;
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
v_sub_parent                    text;
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
        -- Can be negative when subpartitioning and there are parent partitions in the past compared to current timestamp value.
        -- abs() prevents run_maintenence from running on those old parent tables
        v_premade_count = abs(round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval)));
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
            v_premade_count = abs(round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval)));
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
        v_id_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
        v_next_partition_id := v_last_partition_id + v_row.part_interval::bigint;
        -- Can be negative when subpartitioning and there are parent partitions with lower values compared to current id value.
        -- abs() prevents run_maintenence from running on those old parent tables
        WHILE (abs((v_next_partition_id - v_current_partition_id) / v_row.part_interval::bigint)) <= v_row.premake 
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
 * Function to drop child tables from an id-based partition set. 
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */
CREATE OR REPLACE FUNCTION drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                  boolean;
v_child_table               text;
v_control                   text;
v_drop_count                int := 0;
v_id_position               int;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_max                       bigint;
v_old_search_path           text;
v_part_interval             bigint;
v_partition_id              bigint;
v_retention                 bigint;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
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
        part_interval::bigint
        , control
        , retention::bigint
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_part_interval
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'id-static' OR type = 'id-dynamic') 
    AND retention IS NOT NULL;

    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
     SELECT  
        part_interval::bigint
        , control
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
    INTO
        v_part_interval
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND (type = 'id-static' OR type = 'id-dynamic'); 
    v_retention := p_retention;

    IF v_part_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
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

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP ID PARTITION: '|| p_parent_table);
END IF;

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT show_partitions FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
        EXECUTE 'SELECT max('||v_control||') FROM '||v_row_max_id.show_partitions INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;

-- Loop through child tables of the given parent
FOR v_child_table IN 
    SELECT n.nspname||'.'||c.relname FROM pg_inherits i join pg_class c ON i.inhrelid = c.oid join pg_namespace n ON c.relnamespace = n.oid WHERE i.inhparent::regclass = p_parent_table::regclass ORDER BY i.inhrelid ASC
LOOP
    v_id_position := (length(v_child_table) - position('p_' in reverse(v_child_table))) + 2;
    v_partition_id := substring(v_child_table from v_id_position)::bigint;

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_part_interval)) THEN
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Uninherit table '||v_child_table||' from '||p_parent_table);
        END IF;
        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, 'Drop table '||v_child_table);
                END IF;
                EXECUTE 'DROP TABLE '||v_child_table||' CASCADE';
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                FOR v_index IN 
                    SELECT i.indexrelid::regclass AS name
                    , c.conname
                    FROM pg_catalog.pg_index i
                    LEFT JOIN pg_catalog.pg_constraint c ON i.indexrelid = c.conindid 
                    WHERE i.indrelid = v_child_table::regclass
                LOOP
                    IF v_jobmon_schema IS NOT NULL THEN
                        v_step_id := add_step(v_job_id, 'Drop index '||v_index.name||' from '||v_child_table);
                    END IF;
                    IF v_index.conname IS NOT NULL THEN
                        EXECUTE 'ALTER TABLE '||v_child_table||' DROP CONSTRAINT '||v_index.conname;
                    ELSE
                        EXECUTE 'DROP INDEX '||v_index.name;
                    END IF;
                    IF v_jobmon_schema IS NOT NULL THEN
                        PERFORM update_step(v_step_id, 'OK', 'Done');
                    END IF;
                END LOOP;
            END IF;
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, 'Moving table '||v_child_table||' to schema '||v_retention_schema);
            END IF;

            EXECUTE 'ALTER TABLE '||v_child_table||' SET SCHEMA '||v_retention_schema; 

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_child_table;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
    PERFORM update_step(v_step_id, 'OK', v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN DROP ID PARTITION: '||p_parent_table||''')' INTO v_job_id;
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
v_datetime_string               text;
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
v_time_position                 int;
v_top_datetime_string           text;
v_top_interval                  interval;
v_top_parent                    text;
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
    , v_datetime_string
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

-- Check if parent table is a subpartition of an already existing time-based partition set managed by pg_partman
-- If so, limit what child tables can be created based on parent suffix
WITH top_oid AS (
    SELECT i.inhparent AS top_parent_oid
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname||'.'||c.relname = p_parent_table
) SELECT n.nspname||'.'||c.relname, p.datetime_string
  INTO v_top_parent, v_top_datetime_string
  FROM pg_catalog.pg_class c
  JOIN top_oid t ON c.oid = t.top_parent_oid
  JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
  JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
  WHERE c.oid = t.top_parent_oid
  AND p.type = 'time-static' OR p.type = 'time-dynamic' OR p.type = 'time-custom';

IF v_top_parent IS NOT NULL THEN 

    SELECT part_interval::interval INTO v_top_interval FROM @extschema@.part_config WHERE parent_table = v_top_parent;

    v_time_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
    IF v_part_interval::interval <> '3 months' OR (v_part_interval::interval = '3 months' AND v_type = 'time-custom') THEN
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
END IF;

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


-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_timestamp_min IS NOT NULL THEN
        IF v_time < v_sub_timestamp_min OR v_time > v_sub_timestamp_max THEN
            CONTINUE;
        END IF;
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
            , v_row.sub_inherit_fk
            , v_row.sub_use_run_maintenance
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
 * Apply constraints managed by partman extension
 */
CREATE OR REPLACE FUNCTION apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT FALSE, p_debug boolean DEFAULT FALSE) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_child_table                   text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_datetime_string               text;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             int; 
v_last_partition_timestamp      timestamp;
v_constraint_values             record;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 text;
v_partition_suffix              text;
v_premake                       int;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN

SELECT type
    , part_interval
    , premake
    , datetime_string
    , constraint_cols
    , jobmon
INTO v_type
    , v_part_interval
    , v_premake
    , v_datetime_string
    , v_constraint_cols
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_constraint_cols IS NULL THEN
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

SELECT show_partitions INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE CONSTRAINT: '||p_parent_table);
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

-- If p_child_table is null, figure out the partition that is the one right before the premake value backwards.
IF p_child_table IS NULL THEN
    
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Automatically determining most recent child on which to apply constraints');
    END IF;

    v_suffix_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;

    IF v_type IN ('time-static', 'time-dynamic', 'time-custom') THEN
        v_last_partition_timestamp := to_timestamp(substring(v_last_partition from v_suffix_position), v_datetime_string);
        v_partition_suffix := to_char(v_last_partition_timestamp - (v_part_interval::interval * ((v_premake * 2)+1) ), v_datetime_string);
    ELSIF v_type IN ('id-static', 'id-dynamic') THEN
        v_last_partition_id := substring(v_last_partition from v_suffix_position)::int;
        v_partition_suffix := (v_last_partition_id - (v_part_interval::int * ((v_premake * 2)+1) ))::text; 
    END IF;
    
    v_child_table := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Target child table: '||v_child_table);
    END IF;
ELSE
    v_child_table := p_child_table;
END IF;
    
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Checking if target child table exists');
END IF;

SELECT tablename INTO v_child_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
IF v_child_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', 'Target child table ('||v_child_table||') does not exist. Skipping constraint creation.');
        PERFORM close_job(v_job_id);
        EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Target child table (%) does not exist. Skipping constraint creation.', v_child_table;
    END IF;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOREACH v_col IN ARRAY v_constraint_cols
LOOP
    SELECT c.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint c 
        JOIN pg_catalog.pg_attribute a ON c.conrelid = a.attrelid 
    WHERE conrelid = v_child_table::regclass 
        AND c.conname LIKE 'partmanconstr_%'
        AND c.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ c.conkey 
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying new constraint on column: '||v_col);
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Partman managed constraint already exists on this table ('||v_child_table||') and column ('||v_col||'). Skipping creation.');
        END IF;
        RAISE WARNING 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_table, v_col ;
        CONTINUE;
    END IF;

    -- Ensure column name gets put on end of constraint name to help avoid naming conflicts 
    v_constraint_name := @extschema@.check_name_length('partmanconstr_'||v_child_tablename, p_suffix := '_'||v_col);

    EXECUTE 'SELECT min('||v_col||')::text AS min, max('||v_col||')::text AS max FROM '||v_child_table INTO v_constraint_values;

    IF v_constraint_values IS NOT NULL THEN
        v_sql := concat('ALTER TABLE ', v_child_table, ' ADD CONSTRAINT ', v_constraint_name
            , ' CHECK (', v_col, ' >= ', quote_literal(v_constraint_values.min), ' AND '
            , v_col, ' <= ', quote_literal(v_constraint_values.max), ')' );
        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'New constraint created: '||v_sql);
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Given column ('||v_col||') contains all NULLs. No constraint created');
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Running analyze on partition set: '||p_parent_table);
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Running analyze on partition set: %', p_parent_table;
    END IF;

    EXECUTE 'ANALYZE '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE CONSTRAINT: '||p_parent_table||''')' INTO v_job_id;
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

