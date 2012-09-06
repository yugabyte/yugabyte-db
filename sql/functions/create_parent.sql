CREATE OR REPLACE FUNCTION part.create_parent(p_parent_table text, p_control text, p_type part.partition_type, p_interval text, p_premake int DEFAULT 2, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_first_partition_time  timestamp;
v_interval              interval;
v_next_partition_name   text;
v_next_partition_time   timestamp;
v_tablename             text;

BEGIN

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    RAISE EXCEPTION 'ID partitioning not supported yet. Try again later!';
END IF;

RAISE NOTICE 'test';
EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

CASE
    WHEN p_interval = 'yearly' THEN
        v_interval = '1 year';
    WHEN p_interval = 'monthly' THEN
        v_interval = '1 month';
    WHEN p_interval = 'weekly' THEN
        v_interval = '1 week';
    WHEN p_interval = 'daily' THEN
        v_interval = '1 day';
    WHEN p_interval = 'hourly' THEN
        v_interval = '1 hour';
    WHEN p_interval = 'half-hour' THEN
        v_interval = '30 mins';
    WHEN p_interval = 'quarter-hour' THEN
        v_interval = '15 mins';
    ELSE
       -- v_interval := p_interval::int;
END CASE;

-- TODO do a test to make sure control column is of an appropriate type
-- TODO create array variable here that adds as many timestamp values to the array as equals p_premake
v_first_partition_time := CURRENT_TIMESTAMP;
v_next_partition_time := CURRENT_TIMESTAMP + v_interval;

EXECUTE 'SELECT part.create_partition('||quote_literal(p_parent_table)||','||quote_literal(v_interval)||','||quote_literal(ARRAY[v_first_partition_time, v_next_partition_time])||')' INTO v_next_partition_name;

INSERT INTO part.part_config (parent_table, type, part_interval, control, current_partition) VALUES
        (p_parent_table, p_type, v_interval, p_control, v_next_partition_name);

EXCEPTION
    -- Catch if the conversion of the p_interval parameter to an integer doesn't work
    WHEN invalid_text_representation THEN
        RAISE EXCEPTION 'Check interval parameter to ensure it is either a valid time period or an integer value for serial partitioning: %', SQLERRM;
END
$$;
