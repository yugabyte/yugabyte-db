CREATE OR REPLACE FUNCTION part.create_partition (p_parent_table text, p_control text, p_interval interval, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_datetime_string               text;
v_partition_name                text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_tablename                     text;
v_time                          timestamp;

BEGIN
FOREACH v_time IN ARRAY p_partition_times LOOP
    v_partition_name := p_parent_table || '_p';

    IF p_interval = '1 year' OR p_interval = '1 month' OR p_interval = '1 week' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'IYYY');
        v_datetime_string := 'IYYY';
    END IF;
    IF p_interval = '1 week' OR p_interval = '7 days' THEN
        v_partition_name := v_partition_name || 'w' || to_char(v_time, 'IW');
        v_datetime_string := v_datetime_string || '"w"IW';
    END IF;
    IF p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'MM');
        v_datetime_string := v_datetime_string || '_MM';
    END IF;
    IF p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'DD');
        v_datetime_string := v_datetime_string || '_DD';
    END IF;
    IF p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'HH24');
        IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
            v_partition_name := v_partition_name || '00';
        END IF; 
        v_datetime_string := v_datetime_string || '_HH24MI';
    END IF;
    IF p_interval = '30 mins' THEN
        IF date_part('minute', v_time) < 30 THEN
            v_partition_name := v_partition_name || '00';
        ELSE
            v_partition_name := v_partition_name || '30';
        END IF;
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
    END IF;

-- pull out datetime portion of last partition's tablename
v_partition_timestamp_start := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), v_datetime_string);
v_partition_timestamp_end := to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), v_datetime_string) + p_interval;

RAISE NOTICE 'v_datetime_string: %',v_datetime_string;
RAISE NOTICE 'v_partition_name: %', v_partition_name;
RAISE NOTICE 'v_partition_timestamp_start: %', v_partition_timestamp_start;
RAISE NOTICE 'v_partition_timestamp_end: %', v_partition_timestamp_end;

IF position('.' in p_parent_table) > 0 THEN 
    v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
END IF;

EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING CONSTRAINTS) INHERITS ('||p_parent_table||')';
EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
    CHECK ('||p_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||p_control||'<'||quote_literal(v_partition_timestamp_end)||')';

---- Call post_script() for given parent table

END LOOP;

RETURN v_partition_name;

END
$$;
