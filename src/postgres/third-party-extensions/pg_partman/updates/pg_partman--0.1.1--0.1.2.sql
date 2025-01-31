-- Added support for quarterly time partitioning (trickier than it first appeared)
-- Fixed bug in run_maintenance() that would give an invalid cast to integer error.
-- Fixed some calls to pg_jobmon that were outside the checks to see if it's actually installed
-- Properly reset search path back to original before partman functions were run if pg_jobmon is being used
-- Changed the default premake to 4 instead of 3. This will cause pg_jobmon's default monitoring for 3 consecutive failing jobs to trigger an before the last premade partition is used up.
-- Added optional jobmon logging to run_maintenance() so that if it fails, pg_jobmon can notify that maintenance didn't work.

ALTER TABLE @extschema@.part_config ALTER premake SET DEFAULT 4;


CREATE OR REPLACE FUNCTION create_parent(p_parent_table text, p_control text, p_type @extschema@.partition_type, p_interval text, p_premake int DEFAULT 4, p_debug boolean DEFAULT false) RETURNS void
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
v_starting_partition_id bigint;
v_step_id               bigint;
v_tablename             text;
v_time_interval         interval;

BEGIN

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
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
        ELSE
            RAISE EXCEPTION 'Invalid interval for time based partitioning: %', p_interval;
        END IF;
END CASE;

IF p_type = 'time-static' OR p_type = 'time-dynamic' THEN
    FOR i IN 0..p_premake LOOP
        v_partition_time := array_append(v_partition_time, quote_literal(CURRENT_TIMESTAMP + (v_time_interval*i))::timestamp);
    END LOOP;

    EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||quote_literal(v_time_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_time)||')' INTO v_last_partition_name;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, datetime_string, last_partition) VALUES
        (p_parent_table, p_type, v_time_interval, p_control, p_premake, v_datetime_string, v_last_partition_name);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    -- If there is already data, start partitioning with the highest current value
    EXECUTE 'SELECT COALESCE(max('||p_control||')::bigint, 0) FROM '||p_parent_table||' LIMIT 1' INTO v_max;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        v_partition_id = array_append(v_partition_id, (v_id_interval*i)+v_starting_partition_id);
    END LOOP;

    EXECUTE 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(p_control)||','
        ||v_id_interval||','||quote_literal(v_partition_id)||')' INTO v_last_partition_name;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, last_partition) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake, v_last_partition_name);

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
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE PARENT: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition creation for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'BAD', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_control text, p_interval interval, p_datetime_string text, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_partition_name                text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_step_id                       bigint;
v_tablename                     text;
v_time                          timestamp;
v_year                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    v_partition_name := p_parent_table || '_p';

    IF p_interval = '1 year' OR p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'YYYY');
        
        IF p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
            v_partition_name := v_partition_name || '_' || to_char(v_time, 'MM');

            IF p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                v_partition_name := v_partition_name || '_' || to_char(v_time, 'DD');

                IF p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                    v_partition_name := v_partition_name || '_' || to_char(v_time, 'HH24');
                    IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
                        v_partition_name := v_partition_name || '00';
                    ELSIF p_interval = '15 mins' THEN
                        IF date_part('minute', v_time) < 15 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSIF date_part('minute', v_time) >= 15 AND date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '15';
                        ELSIF date_part('minute', v_time) >= 30 AND date_part('minute', v_time) < 45 THEN
                            v_partition_name := v_partition_name || '30';
                        ELSE
                            v_partition_name := v_partition_name || '45';
                        END IF;
                    ELSIF p_interval = '30 mins' THEN
                        IF date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSE
                            v_partition_name := v_partition_name || '30';
                        END IF;
                    END IF;
                END IF; -- end hour IF      
            END IF; -- end day IF
        END IF; -- end month IF
    ELSIF p_interval = '1 week' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
    END IF; -- end year/week IF

    -- pull out datetime portion of last partition's tablename
    v_partition_timestamp_start := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string);
    v_partition_timestamp_end := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string) + p_interval;

    -- "Q" is ignored in to_timestamp, so handle special case
    IF p_interval = '3 months' THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_name := v_partition_name || v_year || 'q' || v_quarter;
        CASE 
            WHEN v_quarter = '1' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                v_partition_timestamp_start := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        END CASE;
        v_partition_timestamp_end := v_partition_timestamp_start + p_interval;
    END IF;

    SELECT schemaname ||'.'|| tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    IF position('.' in p_parent_table) > 0 THEN 
        v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING INDEXES)';
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||p_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||p_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'BAD', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


CREATE OR REPLACE FUNCTION create_time_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_1st_partition_name            text;
v_1st_partition_timestamp       timestamp;
v_2nd_partition_name            text;
v_2nd_partition_timestamp       timestamp;
v_control                       text;
v_current_partition_name        text;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_final_partition_timestamp     timestamp;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_part_interval                 interval;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamp;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;


BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT type
    , part_interval::interval
    , control
    , datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_type = 'time-static' THEN

    CASE
        WHEN v_part_interval = '15 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_part_interval = '30 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_part_interval = '1 hour' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_part_interval = '1 day' THEN
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 week' THEN
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 month' THEN
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '3 months' THEN
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 year' THEN
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;
    
    v_prev_partition_timestamp := v_current_partition_timestamp - v_part_interval::interval;    
    v_1st_partition_timestamp := v_current_partition_timestamp + v_part_interval::interval;
    v_2nd_partition_timestamp := v_1st_partition_timestamp + v_part_interval::interval;
    v_final_partition_timestamp := v_2nd_partition_timestamp + v_part_interval::interval;

    v_prev_partition_name := p_parent_table || '_p' || to_char(v_prev_partition_timestamp, v_datetime_string);
    v_current_partition_name := p_parent_table || '_p' || to_char(v_current_partition_timestamp, v_datetime_string);
    v_1st_partition_name := p_parent_table || '_p' || to_char(v_1st_partition_timestamp, v_datetime_string);
    v_2nd_partition_name := p_parent_table || '_p' || to_char(v_2nd_partition_timestamp, v_datetime_string);

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||quote_literal(v_current_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_1st_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||quote_literal(v_1st_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_2nd_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_1st_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||quote_literal(v_2nd_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_final_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_2nd_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||quote_literal(v_prev_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_current_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); 
            ELSE 
                RETURN NEW; 
            END IF; 
        END IF; 
        RETURN NULL; 
        END $t$;';

--    RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current time interval: '||v_current_partition_timestamp||' to '||(v_1st_partition_timestamp-'1sec'::interval));
    END IF;

ELSIF v_type = 'time-dynamic' THEN

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_partition_name        text;
            v_partition_timestamp   timestamp;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            ';
        CASE
            WHEN v_part_interval = '15 mins' THEN 
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''15min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 15.0);';
            WHEN v_part_interval = '30 mins' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''30min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 30.0);';
            WHEN v_part_interval = '1 hour' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||');';
             WHEN v_part_interval = '1 day' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''day'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 week' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''week'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 month' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''month'', NEW.'||v_control||');';
            WHEN v_part_interval = '3 months' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''quarter'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 year' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''year'', NEW.'||v_control||');';
        END CASE;

        v_trig_func := v_trig_func||'
            v_partition_name := '''||p_parent_table||'_p''|| to_char(v_partition_timestamp, '||quote_literal(v_datetime_string)||');
        
            EXECUTE ''INSERT INTO ''||v_partition_name||'' VALUES($1.*)'' USING NEW;
        END IF;
        
        RETURN NULL; 
        END $t$;';

    --RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic time table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid time partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'BAD', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


CREATE OR REPLACE FUNCTION create_next_time_partition (p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition            text;
v_next_partition_timestamp  timestamp;
v_next_year                 text;
v_part_interval             interval;
v_quarter                   text;
v_tablename                 text;
v_type                      @extschema@.partition_type;
v_year                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
    , last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string, v_last_partition;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

-- Double check that last created partition exists
IF v_last_partition IS NOT NULL THEN
    SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = v_last_partition;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'ERROR: previous partition table missing. Unable to determine next proper partition in sequence.';
    END IF;
ELSE
    RAISE EXCEPTION 'ERROR: last known partition missing from config table for parent table %.', p_parent_table;
END IF;

-- pull out datetime portion of last partition's tablename to make the next one
IF v_part_interval != '3 months' THEN
    v_next_partition_timestamp := to_timestamp(substring(v_last_partition from char_length(p_parent_table||'_p')+1), v_datetime_string) + v_part_interval;
ELSE
    -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
    v_year := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 1);
    v_next_year := extract('year' from to_date(v_year, 'YYYY')+'1year'::interval); 
    v_quarter := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 2);
    CASE
        WHEN v_quarter = '1' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
        WHEN v_quarter = '2' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
        WHEN v_quarter = '3' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        WHEN v_quarter = '4' THEN
            v_next_partition_timestamp := to_timestamp(v_next_year || '-01-01', 'YYYY-MM-DD');
    END CASE;
END IF;

EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','||quote_literal(v_part_interval)||','
    ||quote_literal(v_datetime_string)||','||quote_literal(ARRAY[v_next_partition_timestamp])||')' INTO v_last_partition; 

IF v_last_partition IS NOT NULL THEN
    UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = p_parent_table;
END IF;

END
$$;


CREATE OR REPLACE FUNCTION create_prev_time_partition(p_parent_table text, p_batch int DEFAULT 1) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition_name       text;
v_max_partition_timestamp   timestamp;
v_min_control               timestamp;
v_min_partition_timestamp   timestamp;
v_part_interval             interval;
v_partition_timestamp       timestamp[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
IF v_min_control IS NULL THEN
    RETURN 0;
END IF;

CASE
    WHEN v_part_interval = '15 mins' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
            '15min'::interval * floor(date_part('minute', v_min_control) / 15.0);
    WHEN v_part_interval = '30 mins' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
            '30min'::interval * floor(date_part('minute', v_min_control) / 30.0);
    WHEN v_part_interval = '1 hour' THEN
        v_min_partition_timestamp := date_trunc('hour', v_min_control);
    WHEN v_part_interval = '1 day' THEN
        v_min_partition_timestamp := date_trunc('day', v_min_control);
    WHEN v_part_interval = '1 week' THEN
        v_min_partition_timestamp := date_trunc('week', v_min_control);
    WHEN v_part_interval = '1 month' THEN
        v_min_partition_timestamp := date_trunc('month', v_min_control);
    WHEN v_part_interval = '3 months' THEN
        v_min_partition_timestamp := date_trunc('quarter', v_min_control);
    WHEN v_part_interval = '1 year' THEN
        v_min_partition_timestamp := date_trunc('year', v_min_control);
END CASE;

-- Subtract 1 so that batch count number actually makes sense
FOR i IN 0..p_batch-1 LOOP
    v_partition_timestamp := ARRAY[(v_min_partition_timestamp + (v_part_interval*i))::timestamp];
RAISE NOTICE 'v_partition_timestamp: %',v_partition_timestamp;
    v_max_partition_timestamp := v_min_partition_timestamp + (v_part_interval*(i+1));
RAISE NOTICE 'v_max_partition_timestamp: %',v_max_partition_timestamp;

    v_sql := 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||quote_literal(v_part_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_timestamp)||')';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    -- create_time_partition() already checks to see if the partition exists and skips creation if it does. 
    -- So this function will still work with already existing partitions to get all data moved out of parent table up to and including when
    -- pg_partman was used to set up partitioning.

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_partition_timestamp)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql;

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

RETURN v_total_rows;

END
$$;


CREATE OR REPLACE FUNCTION run_maintenance() RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
v_count                         int := 0;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 real;
v_quarter                       text;
v_step_id                       bigint;
v_row                           record;
v_year                          text;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
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
FROM @extschema@.part_config WHERE type = 'time-static' OR type = 'time-dynamic'
LOOP
    
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

    IF v_row.part_interval != '3 months' THEN
        v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), v_row.datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), 'q', 1);
        v_quarter := split_part(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), 'q', 2);
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

    -- Check and see how many premade partitions there are. If it's less than premake in config table, make another
    v_premade_count = EXTRACT('epoch' FROM (v_last_partition_timestamp - v_current_partition_timestamp)::interval) / EXTRACT('epoch' FROM v_row.part_interval::interval);

    IF v_premade_count < v_row.premake THEN
        RAISE NOTICE 'Creating next partition';
        EXECUTE 'SELECT @extschema@.create_next_time_partition('||quote_literal(v_row.parent_table)||')';

        IF v_row.type = 'time-static' THEN
            EXECUTE 'SELECT @extschema@.create_time_function('||quote_literal(v_row.parent_table)||')';
        END IF;
    END IF;

    v_count := v_count + 1;

END LOOP; -- end of main loop

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_count||' partitions made');
    PERFORM close_job(v_job_id);
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN RUN MAINTENANCE');
                v_step_id := add_step(v_job_id, 'EXCEPTION before job logging started');
            END IF;
            IF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;
