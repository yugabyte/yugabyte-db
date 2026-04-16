CREATE FUNCTION @extschema@.show_partition_info(p_child_table text
    , p_partition_interval text DEFAULT NULL
    , p_parent_table text DEFAULT NULL
    , OUT child_start_time timestamptz
    , OUT child_end_time timestamptz
    , OUT child_start_id bigint
    , OUT child_end_id bigint 
    , OUT suffix text)
RETURNS record
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_child_schema          text;
v_child_tablename       text;
v_start_time_string     text; 
v_control               text;
v_control_type          text;
v_datetime_string       text;
v_epoch                 text;
v_parent_table          text;
v_partition_interval    text;
v_partition_type        text;
v_quarter               text;
v_start_time_epoch      double precision;
v_suffix                text;
v_suffix_position       int;
v_year                  text;

BEGIN
/*
 * Show the data boundries for a given child table as well as the suffix that will be used.
 * Passing the parent table argument improves performance by avoiding a catalog lookup.
 * Passing an interval lets you set one different than the default configured one if desired.
 */

SELECT n.nspname, c.relname INTO v_child_schema, v_child_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_child_table, '.', 1)::name
AND c.relname = split_part(p_child_table, '.', 2)::name;

IF v_child_tablename IS NULL THEN
    RAISE EXCEPTION 'Child table given does not exist (%)', p_child_table;
END IF;

IF p_parent_table IS NULL THEN
    SELECT n.nspname||'.'|| c.relname INTO v_parent_table
    FROM pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhparent
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhrelid::regclass = p_child_table::regclass;
ELSE
    v_parent_table := p_parent_table;
END IF;

IF p_partition_interval IS NULL THEN
    SELECT control, partition_interval, partition_type, datetime_string, epoch
    INTO v_control, v_partition_interval, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
ELSE
    v_partition_interval := p_partition_interval;
    SELECT control, partition_type, datetime_string, epoch 
    INTO v_control, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
END IF;

IF v_control IS NULL THEN
    RAISE EXCEPTION 'Parent table of given child not managed by pg_partman: %', v_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_child_schema, v_child_tablename, v_control);

v_suffix_position := (length(v_child_tablename) - position('p_' in reverse(v_child_tablename))) + 2;
v_suffix := substring(v_child_tablename from v_suffix_position);

RAISE DEBUG 'show_partition_info: v_child_schema: %, v_child_tablename: %, v_suffix: %',
            v_child_schema, v_child_tablename, v_suffix;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

    IF v_partition_type = 'native' THEN
    -- Look at actual partition bounds in catalog and pull values from there. 
    -- For native partitioning, handles any possible interval type much better than old methods in remaining conditions of this IF block 
        SELECT (regexp_match(pg_get_expr(c.relpartbound, c.oid, true)
            , $REGEX$\(([^)]+)\) TO \(([^)]+)\)$REGEX$))[1]::text
        INTO v_start_time_string
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n
        ON c.relnamespace = n.oid
        WHERE c.relname = v_child_tablename
        AND n.nspname = v_child_schema;

        IF v_control_type = 'time' THEN
            child_start_time := v_start_time_string::timestamptz;
        ELSIF (v_control_type = 'id' AND v_epoch <> 'none') THEN
            -- bigint data type is stored as a single-quoted string in the partition expression. Must strip quotes for valid type-cast.
            v_start_time_string := trim(BOTH '''' FROM v_start_time_string);
            IF v_epoch = 'seconds' THEN
                child_start_time := to_timestamp(v_start_time_string::double precision);
            ELSIF v_epoch = 'milliseconds' THEN
                child_start_time := to_timestamp((v_start_time_string::double precision) / 1000);
            ELSIF v_epoch = 'nanoseconds' THEN
                child_start_time := to_timestamp((v_start_time_string::double precision) / 1000000000);
            END IF;
        ELSE
            RAISE EXCEPTION 'Unexpected code path in show_partition_info(). Please report this bug with the configuration that lead to it.';
        END IF;

    ELSIF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
       child_start_time := to_timestamp(v_suffix, v_datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(v_suffix, 'q', 1);
        v_quarter := split_part(v_suffix, 'q', 2);
        CASE
            WHEN v_quarter = '1' THEN
                child_start_time := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                child_start_time := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                child_start_time := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                child_start_time := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            ELSE
                -- handle case when partition name did not use "q" convetion
                child_start_time := to_timestamp(v_suffix, v_datetime_string);
        END CASE;
    END IF;

    child_end_time := (child_start_time + v_partition_interval::interval);

ELSIF v_control_type = 'id' THEN

    -- Note for future updates, if trigger based partitioning is dropped, do catalog lookup for integer boundaries similar to time.
    child_start_id := v_suffix::bigint;
    child_end_id := (child_start_id + v_partition_interval::bigint) - 1;

ELSE
    RAISE EXCEPTION 'Invalid partition type encountered in show_partition_info()';
END IF;

suffix = v_suffix;

RETURN;

END
$$;

