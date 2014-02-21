/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE FUNCTION create_id_function(p_parent_table text) RETURNS void
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
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
    , premake
    , last_partition
    , jobmon
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_last_partition
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

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
    EXECUTE 'SELECT COALESCE(max('||v_control||'), 0) FROM '||p_parent_table INTO v_max;
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
        BEGIN
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||v_current_partition_id||' AND NEW.'||v_control||' < '||v_next_partition_id|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';

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
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*); ';
            END IF;
        END LOOP;

        v_trig_func := v_trig_func ||'
            ELSE
                RETURN NEW;
            END IF;
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_next_partition_id := (substring(v_last_partition from v_id_position)::bigint) + '||v_part_interval||';
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||');
                    PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
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
            v_next_partition_name       text;   
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := @extschema@.check_name_length('''||v_parent_tablename||''', '''||v_parent_schema||''', v_current_partition_id::text, TRUE);
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF NEW.'||v_control||' >= v_next_partition_id THEN
                    RETURN NEW;
                END IF;
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    IF v_next_partition_name IS NOT NULL THEN
                        UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                        PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||');
                        PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_current_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;
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


