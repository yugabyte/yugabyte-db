CREATE OR REPLACE FUNCTION part.run_maintenance() RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_datetime_string               text;
v_current_partition_timestamp   timestamp;
v_row                           record;
v_sql                           text;

BEGIN

v_sql := 'SELECT parent_table
    , type
    , part_interval::interval
    , control
    , last_partition
FROM part.part_config where type = ''time-static'' or type = ''time-dynamic''';

FOR v_row IN 
SELECT parent_table
    , type
    , part_interval::interval
    , control
    , last_partition
FROM part.part_config WHERE type = 'time-static' OR type = 'time-dynamic'
LOOP
    
    CASE
        WHEN v_row.part_interval = '15 mins' THEN
            v_datetime_string := 'YYYY_MM_DD_HH24MI';
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_row.part_interval = '30 mins' THEN
            v_datetime_string := 'YYYY_MM_DD_HH24MI';
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_row.part_interval = '1 hour' THEN
            v_datetime_string := 'YYYY_MM_DD_HH24MI';
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_row.part_interval = '1 day' THEN
            v_datetime_string := 'YYYY_MM_DD';
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 week' THEN
            v_datetime_string := 'IYYY"w"IW';
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 month' THEN
            v_datetime_string := 'YYYY_MM';
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 year' THEN
            v_datetime_string := 'YYYY';
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;

    -- If it's been longer than the parent table's destinated interval, make the next partition
    RAISE NOTICE 'It''s been this long: %', (CURRENT_TIMESTAMP - v_current_partition_timestamp)::interval;

    IF ((CURRENT_TIMESTAMP - v_current_partition_timestamp)::interval >= v_row.part_interval) THEN
        RAISE NOTICE 'Creating next partition';
        EXECUTE 'SELECT part.create_next_time_partition('||quote_literal(v_row.parent_table)||')';

        IF v_row.type = 'time-static' THEN
            EXECUTE 'SELECT part.create_time_function('||quote_literal(v_row.parent_table)||')';
        END IF;
    END IF;

    

END LOOP; -- end of main loop

END
$$;
