CREATE FUNCTION create_id_function(p_parent_table text, p_current_id bigint) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_1st_partition_name            text;
v_1st_partition_id              bigint;
v_2nd_partition_name            text;
v_2nd_partition_id              bigint;
v_control                       text;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition                text;
v_old_search_path               text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_name           text;
v_prev_partition_id             bigint;
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
    , part_interval::bigint
    , control
    , premake
    , last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic')
INTO v_type, v_part_interval, v_control, v_premake, v_last_partition;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_type = 'id-static' THEN
    v_current_partition_id := p_current_id - (p_current_id % v_part_interval);
    v_prev_partition_id := v_current_partition_id - v_part_interval;    
    v_1st_partition_id := v_current_partition_id + v_part_interval;
    v_2nd_partition_id := v_1st_partition_id + v_part_interval;
    v_final_partition_id := v_2nd_partition_id + v_part_interval;

    v_prev_partition_name := p_parent_table || '_p' || v_prev_partition_id::text;
    v_current_partition_name := p_parent_table || '_p' || v_current_partition_id::text;
    v_1st_partition_name := p_parent_table || '_p' || v_1st_partition_id::text;
    v_2nd_partition_name := p_parent_table || '_p' || v_2nd_partition_id::text;

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id  bigint;
            v_last_partition        text := '||quote_literal(v_last_partition)||';
            v_next_partition_id     bigint;
            v_next_partition_name   text;         
        BEGIN
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||v_current_partition_id||' AND NEW.'||v_control||' < '||v_1st_partition_id|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||v_1st_partition_id||' AND NEW.'||v_control||' < '||v_2nd_partition_id|| ' THEN 
                INSERT INTO '||v_1st_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||v_2nd_partition_id||' AND NEW.'||v_control||' < '||quote_literal(v_final_partition_id)|| ' THEN 
                INSERT INTO '||v_2nd_partition_name||' VALUES (NEW.*);
            ';
            -- If the first partition's function, don't have rule for previous partition
            IF v_prev_partition_id >= 0 THEN
            v_trig_func := v_trig_func ||'ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_current_partition_id|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*);
            ';
            END IF;

            v_trig_func := v_trig_func ||'ELSE
                RETURN NEW;
            END IF;
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_next_partition_id := (substring(v_last_partition from char_length('||quote_literal(p_parent_table||'_p')||')+1)::bigint) + '||v_part_interval||';
                IF ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' THEN 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||v_part_interval||', ARRAY[v_next_partition_id]);
                    UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                END IF;
            END IF;
        END IF; 
        RETURN NULL; 
        END $t$;';

    --RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current id interval: '||v_current_partition_id||' to '||v_1st_partition_id-1);
    END IF;

ELSIF v_type = 'id-dynamic' THEN

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_last_partition            text := '||quote_literal(v_last_partition)||';
            v_last_partition_id         bigint;
            v_next_partition_id         bigint;
            v_next_partition_name       text;   
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := '''||p_parent_table||'_p''||v_current_partition_id;

            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_last_partition_id = substring(v_last_partition from char_length('||quote_literal(p_parent_table||'_p')||')+1)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF ((v_next_partition_id - v_current_partition_id) / '||quote_literal(v_part_interval)||') <= '||quote_literal(v_premake)||' THEN 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||quote_literal(v_part_interval)||', ARRAY[v_next_partition_id]);
                    IF v_next_partition_name IS NOT NULL THEN
                        UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                        PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                    END IF;
                END IF;
            END IF;
            EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
        END IF;
        
        RETURN NULL; 
        END $t$;';

--    RAISE NOTICE 'v_trig_func: %',v_trig_func;
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
