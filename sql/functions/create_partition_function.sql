CREATE OR REPLACE FUNCTION part.create_part_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_current_partition             text;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_final_partition_timestamp     timestamp;
v_next_partition_name           text;
v_next_partition_timestamp      timestamp;
v_part_interval                 text;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamp;
v_trig_func                     text;
v_type                          text;


BEGIN

SELECT type
    , part_interval
    , control
    , current_partition
FROM part.part_config WHERE parent_table = p_parent_table
INTO v_type, v_part_interval, v_control, v_current_partition;

CASE
    WHEN v_part_interval = '1 year' THEN
        v_datetime_string := 'YYYY';
    WHEN v_part_interval = '1 month' THEN
        v_datetime_string := 'YYYY_MM';
    WHEN v_part_interval = '1 week' THEN
        v_datetime_string := 'YYYYwWW';
    WHEN v_part_interval = '1 day' THEN
        v_datetime_string := 'YYYY_MM_DD';
    WHEN v_part_interval = '1 hour' OR v_part_interval = '30 mins' OR v_part_interval = '15 mins' THEN
        v_datetime_string := 'YYYY_MM_DD_HH24MI';
END CASE;

IF v_type = 'time-static' THEN
    
    v_current_partition_timestamp := to_timestamp(substring(v_current_partition from char_length(p_parent_table||'_p')+1), v_datetime_string);    
    v_prev_partition_timestamp := v_current_partition_timestamp - v_part_interval::interval;    
    v_next_partition_timestamp := v_current_partition_timestamp + v_part_interval::interval;
    v_final_partition_timestamp := v_next_partition_timestamp + v_part_interval::interval;

    v_prev_partition_name := p_parent_table || '_p' || to_char(v_prev_partition_timestamp, v_datetime_string);
    v_next_partition_name := p_parent_table || '_p' || to_char(v_next_partition_timestamp, v_datetime_string);

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ ';
    v_trig_func := v_trig_func||'DECLARE ';
    v_trig_func := v_trig_func||'BEGIN ';
    v_trig_func := v_trig_func||'IF TG_OP = ''INSERT'' THEN ';
    v_trig_func := v_trig_func||'IF NEW.'||v_control||' >= '||quote_literal(v_current_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_next_partition_timestamp)|| ' THEN ';
    v_trig_func := v_trig_func||'INSERT INTO '||v_current_partition||' VALUES (NEW.*); ';
    v_trig_func := v_trig_func||'ELSIF NEW.'||v_control||' >= '||quote_literal(v_next_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_final_partition_timestamp)|| ' THEN ';
    v_trig_func := v_trig_func||'INSERT INTO '||v_next_partition_name||' VALUES (NEW.*); ';
    v_trig_func := v_trig_func||'ELSIF NEW.'||v_control||' >= '||quote_literal(v_prev_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_current_partition_timestamp)|| ' THEN ';
    v_trig_func := v_trig_func||'INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); ';
    v_trig_func := v_trig_func||'ELSE ';
    v_trig_func := v_trig_func||'RAISE EXCEPTION ''ERROR: Attempt to insert data into parent table outside partition trigger boundaries: %'', NEW.'||v_control||';';
    v_trig_func := v_trig_func||' END IF; ';
    v_trig_func := v_trig_func||' END IF; ';
    v_trig_func := v_trig_func||' RETURN NULL; ';
    v_trig_func := v_trig_func||'END $t$;';

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
