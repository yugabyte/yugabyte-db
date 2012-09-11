CREATE OR REPLACE FUNCTION part.create_id_function(p_parent_table text, p_current_id bigint) RETURNS void
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
v_last_partition                text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_name           text;
v_prev_partition_id             bigint;
v_trig_func                     text;
v_type                          text;


BEGIN

SELECT type
    , part_interval::bigint
    , control
    , premake
    , last_partition
FROM part.part_config 
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
            -- There
            IF v_prev_partition_id >= 0 THEN
            v_trig_func := v_trig_func ||'ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_current_partition_id|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*);
                ';
            END IF;

            v_trig_func := v_trig_func ||'ELSE 
                RAISE EXCEPTION ''ERROR: Attempt to insert data into parent table outside partition trigger boundaries: %'', NEW.'||v_control||'; 
            END IF;
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_next_partition_id := (substring(v_last_partition from char_length('||quote_literal(p_parent_table||'_p')||')+1)::bigint) + '||v_part_interval||';
                IF ((v_next_partition_id - v_current_partition_id) / '||quote_literal(v_part_interval)||') <= '||quote_literal(v_premake)||' THEN 
                    v_next_partition_name := part.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||quote_literal(v_part_interval)||', ARRAY[v_next_partition_id]);
                    UPDATE part.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM part.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                END IF;
            END IF;
        END IF; 
        RETURN NULL; 
        END $t$;';

    RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

ELSIF v_type = 'id-dynamic' THEN

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_counter                   int; 
            v_new_partition_name        text;
            v_new_partition_timestamp   timestamp;
            v_old_partition_name        text;
            v_old_partition_timestamp   timestamp;
            v_row                       record;
            v_where                     text;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            ';
        CASE
            WHEN v_part_interval = '15 mins' THEN 
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''15min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 15.0);';
            WHEN v_part_interval = '30 mins' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''30min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 30.0);';
            WHEN v_part_interval = '1 hour' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||');';
             WHEN v_part_interval = '1 day' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''day'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 week' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''week'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 month' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''month'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 year' THEN
                v_trig_func := v_trig_func||'v_new_partition_timestamp := date_trunc(''year'', NEW.'||v_control||');';
        END CASE;

        v_trig_func := v_trig_func||'
            v_new_partition_name := '''||p_parent_table||'_p''|| to_char(v_new_partition_timestamp, '||quote_literal(v_datetime_string)||');
        
            EXECUTE ''INSERT INTO ''||v_new_partition_name||'' VALUES($1.*)'' USING NEW;
        END IF;
        
        RETURN NULL; 
        END $t$;';

    RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid id partitioning type given: %', v_type;
END IF;


END
$$;
