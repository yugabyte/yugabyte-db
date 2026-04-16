-- Fix create_parent() to actually insert the contraint_cols value passed into the function to the config table when using time based partitioning. Thanks to Jeff Amiel for reporting the issue.


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
    , p_debug boolean DEFAULT false) 
RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_current_id            bigint;
v_datetime_string       text;
v_id_interval           bigint;
v_job_id                bigint;
v_jobmon_schema         text;
v_last_partition_name   text;
v_old_search_path       text;
v_partition_time        timestamp[];
v_partition_id          bigint[];
v_max                   bigint;
v_notnull               boolean;
v_starting_partition_id bigint;
v_step_id               bigint;
v_tablename             text;
v_time_interval         interval;
v_valid_types           text[] := '{"time-static", "time-dynamic", "id-static", "id-dynamic"}';

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

IF NOT (string_to_array(p_type, '') <@ v_valid_types) THEN
    RAISE EXCEPTION '% is not a valid partitioning type (%)', p_type, array_to_string(v_valid_types, ', ');
END IF;

EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN SETUP PARENT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating initial partitions on new parent table: '||p_parent_table);
END IF;

CASE
    WHEN p_interval = 'yearly' THEN
        v_time_interval = '1 year';
        v_datetime_string := 'YYYY';
    WHEN p_interval = 'quarterly' THEN
        v_time_interval = '3 months';
        v_datetime_string = 'YYYY"q"Q';
    WHEN p_interval = 'monthly' THEN
        v_time_interval = '1 month';
        v_datetime_string := 'YYYY_MM';
    WHEN p_interval = 'weekly' THEN
        v_time_interval = '1 week';
        v_datetime_string := 'IYYY"w"IW';
    WHEN p_interval = 'daily' THEN
        v_time_interval = '1 day';
        v_datetime_string := 'YYYY_MM_DD';
    WHEN p_interval = 'hourly' THEN
        v_time_interval = '1 hour';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    WHEN p_interval = 'half-hour' THEN
        v_time_interval = '30 mins';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    WHEN p_interval = 'quarter-hour' THEN
        v_time_interval = '15 mins';
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
    ELSE
        IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
            v_id_interval := p_interval::bigint;
            IF v_id_interval <= 0 THEN
                RAISE EXCEPTION 'Interval for serial partitioning must be greater than zero';
            END IF;
        ELSE
            RAISE EXCEPTION 'Invalid interval for time based partitioning: %', p_interval;
        END IF;
END CASE;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
    FOR i IN 0..p_premake LOOP
        IF i > 0 THEN -- also create previous partitions equal to premake, but avoid duplicating current
            v_partition_time := array_append(v_partition_time, quote_literal(CURRENT_TIMESTAMP - (v_time_interval*i))::timestamp);
        END IF;
        v_partition_time := array_append(v_partition_time, quote_literal(CURRENT_TIMESTAMP + (v_time_interval*i))::timestamp);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols, datetime_string) VALUES
        (p_parent_table, p_type, v_time_interval, p_control, p_premake, p_constraint_cols, v_datetime_string);
    EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||quote_literal(v_time_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_time)||')' INTO v_last_partition_name;
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    -- If there is already data, start partitioning with the highest current value
    EXECUTE 'SELECT COALESCE(max('||p_control||')::bigint, 0) FROM '||p_parent_table||' LIMIT 1' INTO v_max;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive
        IF (v_starting_partition_id - (v_id_interval*i)) > 0 AND (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id THEN
            v_partition_id = array_append(v_partition_id, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id = array_append(v_partition_id, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake, p_constraint_cols);
    EXECUTE 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||v_id_interval||','||quote_literal(v_partition_id)||')' INTO v_last_partition_name;
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID partitions premade: '||p_premake);
    END IF;
    
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
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
    PERFORM close_job(v_job_id);
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
