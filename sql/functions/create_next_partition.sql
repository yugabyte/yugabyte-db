CREATE OR REPLACE FUNCTION part.create_next_partition (p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_datetime_string           text;
v_last_created              text;
v_last_partition_name       text;
v_next_partition_timestamp  timestamp;
v_part_interval             interval;
v_tablename                 text;
v_type                      part.partition_type;


BEGIN

SELECT type
    , part_interval
    , last_created
FROM part.part_config WHERE parent_table = p_parent_table
INTO v_type, v_part_interval, v_last_created;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

-- Double check that last created partition exists
IF v_last_created IS NOT NULL THEN
    SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'ERROR: previous partition missing. Unable to determine proper next partition name';
    END IF;
END IF;

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

-- pull out datetime portion of last partition's tablename
v_next_partition_timestamp := to_timestamp(substring(v_last_created from char_length(p_parent_table||'_p')+1), v_datetime_string) + v_part_interval;

EXECUTE 'SELECT part.create_partition('||quote_literal(p_parent_table)||','||quote_literal(v_part_interval)||','||quote_literal(ARRAY[v_next_partition_timestamp])||')' INTO v_last_partition_name; 

UPDATE part.part_config SET last_created = v_last_partition_name WHERE parent_table = p_parent_table;

RAISE NOTICE 'v_last_partition_name: %',v_last_partition_name;
END
$$;
