CREATE OR REPLACE FUNCTION part.create_parent(p_parent_table text, p_scheme part.partition_interval, p_premake int, p_id_series int DEFAULT NULL, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_first_partition_time  timestamp;
v_interval              interval;
v_last_partition_name   text;
v_next_partition_time   timestamp;
v_tablename             text;
v_type                  part.partition_type;

BEGIN

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

IF p_scheme = 'id' THEN
    RAISE EXCEPTION 'ID partitioning not supported yet. Please privide a time interval for partitioning';
    -- v_type = 'id';
ELSE
    v_type = 'time';
END IF;

EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

CASE
    WHEN p_scheme = 'yearly' THEN
        v_interval = '1 year';
    WHEN p_scheme = 'monthly' THEN
        v_interval = '1 month';
    WHEN p_scheme = 'weekly' THEN
        v_interval = '1 week';
    WHEN p_scheme = 'daily' THEN
        v_interval = '1 day';
    WHEN p_scheme = 'hourly' THEN
        v_interval = '1 hour';
    WHEN p_scheme = 'half-hour' THEN
        v_interval = '30 mins';
    WHEN p_scheme = 'quarter-hour' THEN
        v_interval = '15 mins';
END CASE;

-- create array variable here that adds as many timestamp values to the array as equals p_premake
v_first_partition_time := CURRENT_TIMESTAMP;
v_next_partition_time := CURRENT_TIMESTAMP + v_interval;

EXECUTE 'SELECT part.create_partition('||quote_literal(p_parent_table)||','||quote_literal(v_interval)||','||quote_literal(ARRAY[v_first_partition_time, v_next_partition_time])||')' INTO v_last_partition_name;

INSERT INTO part.part_config (parent_table, type, part_interval, last_created) VALUES
        (p_parent_table, v_type, v_interval, v_last_partition_name);

END
$$;
