CREATE OR REPLACE FUNCTION part.create_partition (p_parent_table text, p_interval interval, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_time              timestamp;
v_partition_name    text;

BEGIN
FOREACH v_time IN ARRAY p_partition_times LOOP
    v_partition_name := p_parent_table || '_p';

    IF p_interval = '1 year' OR p_interval = '1 month' OR p_interval = '1 week' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'YYYY');
    END IF;
    IF p_interval = '1 week' THEN
        v_partition_name := v_partition_name || 'w' || to_char(v_time, 'WW');
    END IF;
    IF p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'MM');
    END IF;
    IF p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'DD');
    END IF;
    IF p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || '_' || to_char(v_time, 'HH24');
        IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
            v_partition_name := v_partition_name || '00';
        END IF; 
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

EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING CONSTRAINTS) INHERITS ('||p_parent_table||')';

---- Call post_script() for given parent table

END LOOP;

RETURN v_partition_name;

END
$$;
