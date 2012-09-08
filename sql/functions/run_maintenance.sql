CREATE OR REPLACE FUNCTION part.run_maintenance RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_row       record;

BEGIN

v_sql := 'SELECT parent_table
    , type
    , part_interval
    , control
    , last_partition
FROM part.part_config where type = ''time-static'' or type = ''time-dynamic''';

FOR v_row AS v_sql LOOP

    -- pull out datetime portion of last partition's tablename
    v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from char_length(p_parent_table||'_p')+1), v_datetime_string);

    -- If it's been longer than the parent table's destinated interval, make the next partition
    IF (CURRENT_TIMESTAMP - v_last_partition_timestamp)::interval > v_row.part_interval::interval) THEN
        EXECUTE 'SELECT part.create_next_partition('||v_row.parent_table||')';
    END IF;

    IF v_row.type = 'time-static' THEN
        EXECUTE 'SELECT part.create_function('||v_row.parent_table||')';
    END IF;

END LOOP; -- end of main loop

END
$$;
