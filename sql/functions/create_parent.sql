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
    , p_start_partition text DEFAULT NULL
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_base_timestamp        timestamp;
v_count                 int := 0;
v_current_id            bigint;
v_datetime_string       text;
v_id_interval           bigint;
v_job_id                bigint;
v_jobmon_schema         text;
v_last_partition_name   text;
v_old_search_path       text;
v_partition_time        timestamp;
v_partition_time_array  timestamp[];
v_partition_id          bigint[];
v_max                   bigint;
v_notnull               boolean;
v_start_time            timestamp;
v_starting_partition_id bigint;
v_step_id               bigint;
v_step_overflow_id      bigint;
v_tablename             text;
v_time_interval         interval;

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

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN SETUP PARENT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating initial partitions on new parent table: '||p_parent_table);
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN

v_start_time := COALESCE(p_start_partition::timestamp, CURRENT_TIMESTAMP);

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
    END CASE;

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

    LOOP
        BEGIN
            v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamp;
        EXCEPTION WHEN datetime_field_overflow THEN
            RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                Child partition creation after time % skipped', v_partition_time;
            v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
            PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_partition_time||' skipped');
            CONTINUE;
        END;
        v_partition_time_array := array_append(v_partition_time_array, v_partition_time);
        v_count := v_count + 1;        

        IF CURRENT_TIMESTAMP >= v_partition_time AND CURRENT_TIMESTAMP < (v_partition_time + v_time_interval) THEN
            EXIT; -- leave loop when current partition is added to array
        END IF;
    END LOOP; 

    FOR i IN 1..p_premake LOOP 
        BEGIN
            IF p_start_partition IS NULL THEN -- also create previous partitions equal to premake unless start_partition parameter is set
                v_partition_time_array := array_append(v_partition_time_array, quote_literal(v_base_timestamp - (v_time_interval*i))::timestamp);
            END IF;
        EXCEPTION WHEN datetime_field_overflow THEN
            RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped';
            v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
            PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation skipped');
        END;

        BEGIN
            v_partition_time_array := array_append(v_partition_time_array, quote_literal(v_base_timestamp + (v_time_interval*i))::timestamp);
        EXCEPTION WHEN datetime_field_overflow THEN
            RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped';
            v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
            PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation skipped');
        END;    
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols, datetime_string, jobmon) VALUES
        (p_parent_table, p_type, v_time_interval, p_control, p_premake, p_constraint_cols, v_datetime_string, p_jobmon);
    v_last_partition_name := @extschema@.create_time_partition(p_parent_table, v_partition_time_array);
    -- Doing separate update because create function requires in config table last_partition to be set
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    v_id_interval := p_interval::bigint;
    IF v_id_interval <= 1 THEN
        RAISE EXCEPTION 'Interval for serial partitioning must be greater than 1';
    END IF;
    
    -- If custom start partition is set, use that. 
    -- If custom start is not set and there is already data, start partitioning with the highest current value
    EXECUTE 'SELECT COALESCE('||quote_nullable(p_start_partition::bigint)||', max('||p_control||')::bigint, 0) FROM '||p_parent_table||' LIMIT 1' INTO v_max;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 
            AND (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id = array_append(v_partition_id, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id = array_append(v_partition_id, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols, jobmon) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake, p_constraint_cols, p_jobmon);
    v_last_partition_name := @extschema@.create_id_partition(p_parent_table, v_partition_id);
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID partitions premade: '||p_premake);
    END IF;
    
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN
    EXECUTE 'SELECT @extschema@.create_time_function('||quote_literal(p_parent_table)||')';
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    v_current_id := COALESCE(v_max, 0);    
    EXECUTE 'SELECT @extschema@.create_id_function('||quote_literal(p_parent_table)||','||v_current_id||')';  
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID function created');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition trigger');
END IF;

EXECUTE 'SELECT @extschema@.create_trigger('||quote_literal(p_parent_table)||')';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
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
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE PARENT: '||p_parent_table||')' INTO v_job_id;
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

