/*
 * Create the next partition in sequence for a time-based partition set
 */
CREATE FUNCTION create_next_time_partition (p_parent_table text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition            text;
v_next_partition_timestamp  timestamp;
v_next_year                 text;
v_part_interval             interval;
v_quarter                   text;
v_tablename                 text;
v_type                      text;
v_year                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
    , last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic')
INTO v_type, v_part_interval, v_control, v_datetime_string, v_last_partition;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

-- Double check that last created partition exists
IF v_last_partition IS NOT NULL THEN
    SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = v_last_partition;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'ERROR: previous partition table missing. Unable to determine next proper partition in sequence.';
    END IF;
ELSE
    RAISE EXCEPTION 'ERROR: last known partition missing from config table for parent table %.', p_parent_table;
END IF;

-- pull out datetime portion of last partition's tablename to make the next one
IF v_part_interval != '3 months' THEN
    v_next_partition_timestamp := to_timestamp(substring(v_last_partition from char_length(p_parent_table||'_p')+1), v_datetime_string) + v_part_interval;
ELSE
    -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
    v_year := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 1);
    v_next_year := extract('year' from to_date(v_year, 'YYYY')+'1year'::interval); 
    v_quarter := split_part(substring(v_last_partition from char_length(p_parent_table||'_p')+1), 'q', 2);
    CASE
        WHEN v_quarter = '1' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
        WHEN v_quarter = '2' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
        WHEN v_quarter = '3' THEN
            v_next_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
        WHEN v_quarter = '4' THEN
            v_next_partition_timestamp := to_timestamp(v_next_year || '-01-01', 'YYYY-MM-DD');
    END CASE;
END IF;

EXECUTE 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','||quote_literal(v_part_interval)||','
    ||quote_literal(v_datetime_string)||','||quote_literal(ARRAY[v_next_partition_timestamp])||')' INTO v_last_partition; 

IF v_last_partition IS NOT NULL THEN
    UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = p_parent_table;
END IF;

END
$$;
