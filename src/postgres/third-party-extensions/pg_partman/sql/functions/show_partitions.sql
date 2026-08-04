CREATE FUNCTION @extschema@.show_partitions (p_parent_table text, p_order text DEFAULT 'ASC', p_include_default boolean DEFAULT false) RETURNS TABLE (partition_schemaname text, partition_tablename text)
    LANGUAGE plpgsql STABLE
    SET search_path = @extschema@,pg_temp
    AS $$
DECLARE

v_control               text;
v_control_type          text;
v_datetime_string       text;
v_default_sql           text;
v_epoch                 text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    text;
v_partition_type        text;
v_sql                   text;

BEGIN
/*
 * Function to list all child partitions in a set in logical order.
 * Default partition is not listed by default since that's the common usage internally
 * If p_include_default is set true, default is always listed first.
 */

IF upper(p_order) NOT IN ('ASC', 'DESC') THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'p_order paramter must be one of the following values: ASC, DESC';
END IF;

SELECT partition_type
    , partition_interval
    , datetime_string
    , control
    , epoch
INTO v_partition_type
    , v_partition_interval
    , v_datetime_string
    , v_control
    , v_epoch
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_partition_type IS NULL THEN
    RAISE EXCEPTION 'Given parent table not managed by pg_partman: %', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Given parent table not found in system catalogs: %', p_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

RAISE DEBUG 'show_partitions: v_parent_schema: %, v_parent_tablename: %, v_datetime_string: %', v_parent_schema, v_parent_tablename, v_datetime_string;

v_sql := format('SELECT n.nspname::text AS partition_schemaname, c.relname::text AS partition_name FROM
        pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE h.inhparent = ''%I.%I''::regclass'
    , v_parent_schema
    , v_parent_tablename);

IF v_partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN

    IF p_include_default THEN
        -- Return the default partition immediately as first item in list
        v_default_sql := v_sql || format('
            AND pg_get_expr(relpartbound, c.oid) = ''DEFAULT''');
        RAISE DEBUG 'show_partitions: v_default_sql: %', v_default_sql;
        RETURN QUERY EXECUTE v_default_sql;
    END IF;

    v_sql := v_sql || format('
        AND pg_get_expr(relpartbound, c.oid) != ''DEFAULT''');

END IF;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

    IF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
        v_sql := v_sql || format('
            ORDER BY to_timestamp(substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) ), %L) %s'
            , v_datetime_string
            , p_order);

    ELSE

        -- For quarterly, to_timestamp() doesn't recognize "Q" in datetime string. 
        -- First order by just the year, then order by the quarter number (should be last character in table name)
        v_sql := v_sql || format('
            ORDER BY to_timestamp(substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) for 4), ''YYYY'') %s
            , substring(reverse(c.relname) from 1 for 1) %s'
            , p_order
            , p_order);

    END IF;

ELSIF v_control_type = 'id' THEN

    v_sql := v_sql || format('
        ORDER BY substring(c.relname from ((length(c.relname) - position(''p_'' in reverse(c.relname))) + 2) )::bigint %s'
        , p_order);

END IF;

RAISE DEBUG 'show_partitions: v_sql: %', v_sql;

RETURN QUERY EXECUTE v_sql;

END
$$;

