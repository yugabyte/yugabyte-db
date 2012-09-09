CREATE OR REPLACE FUNCTION part.create_time_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_current_partition_name        text;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_final_partition_timestamp     timestamp;
v_1st_partition_name            text;
v_1st_partition_timestamp       timestamp;
v_2nd_partition_name            text;
v_2nd_partition_timestamp       timestamp;
v_part_interval                 interval;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamp;
v_trig_func                     text;
v_type                          text;


BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
FROM part.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

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
    WHEN v_part_interval = '1 year' THEN
        v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
END CASE;

IF v_type = 'time-static' THEN
    
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
                RAISE EXCEPTION ''ERROR: Attempt to insert data into parent table outside partition trigger boundaries: %'', NEW.'||v_control||'; 
            END IF; 
        END IF; 
        RETURN NULL; 
        END $t$;';

--    RAISE NOTICE 'v_trig_func: %',v_trig_func;
    EXECUTE v_trig_func;

ELSIF v_type = 'id-static' THEN

ELSIF v_type = 'time-dynamic' THEN

ELSIF v_type = 'id-dynamic' THEN

ELSE
    RAISE EXCEPTION 'ERROR: Invalid partitioning type given: %', v_type;
END IF;


END
$$;
