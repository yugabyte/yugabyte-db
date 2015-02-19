-- Updated reapply_indexes.py script to, by default, only add new indexes and drop ones that don't exist on the parent. Previously it would drop all indexes on all children and recreate them to match the parent. Now it only does the minimal amount of work to make the children match the parent. An additional option (--recreate_all/-R) was added to allow the old behavior of redoing all indexes from scratch if desired.
-- Changed the minimal interval that serial partitioning can be done to 10. Ran into issues with an interval of 2 and partitioning anything this low is unrealistic and provides no benefit. 10 seems like a reasonable minimal to have at this point to avoid any future issues. This does not affect any existing partition sets, only newly created ones.
-- Serial partition maintenance when using run_maintenance() is now much more efficient. Should run significantly faster for very large partition sets.


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
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;
        IF v_top_parent IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            v_time_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
            v_parent_partition_timestamp := to_timestamp(substring(p_parent_table from v_time_position), v_datetime_string);
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

    EXECUTE 'SELECT COALESCE(max('||v_control||'), 0) FROM '||v_top_parent INTO v_max;
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
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitons made. '||v_drop_count||' partitions dropped.');
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

