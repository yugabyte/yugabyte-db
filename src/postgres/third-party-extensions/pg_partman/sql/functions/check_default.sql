CREATE FUNCTION @extschema@.check_default(p_exact_count boolean DEFAULT true) RETURNS SETOF @extschema@.check_default_table
    LANGUAGE plpgsql STABLE
    SET search_path = @extschema@,pg_temp
    AS $$
DECLARE

v_count                     bigint = 0;
v_default_schemaname        text;
v_default_tablename         text;
v_parent_schemaname         text;
v_parent_tablename          text;
v_row                       record;
v_sql                       text;
v_trouble                   @extschema@.check_default_table%rowtype;

BEGIN
/*
 * Function to monitor for data getting inserted into parent/default tables 
 */

FOR v_row IN 
    SELECT parent_table, partition_type FROM @extschema@.part_config
LOOP
    SELECT schemaname, tablename
    INTO v_parent_schemaname, v_parent_tablename
    FROM pg_catalog.pg_tables 
    WHERE schemaname = split_part(v_row.parent_table, '.', 1)::name
    AND tablename = split_part(v_row.parent_table, '.', 2)::name;

    IF v_row.partition_type = 'partman' THEN
        -- trigger based checks parent table
        IF p_exact_count THEN
            v_sql := format('SELECT count(1) AS n FROM ONLY %I.%I', v_parent_schemaname, v_parent_tablename);
        ELSE
            v_sql := format('SELECT count(1) AS n FROM (SELECT 1 FROM ONLY %I.%I LIMIT 1) x', v_parent_schemaname, v_parent_tablename);
        END IF;
        EXECUTE v_sql INTO v_count;

        IF v_count > 0 THEN 
            v_trouble.default_table := v_parent_schemaname ||'.'|| v_parent_tablename;
            v_trouble.count := v_count;
            RETURN NEXT v_trouble;
        END IF;

    ELSIF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        -- native PG11+ checks default if it exists

        v_sql := format('SELECT n.nspname::text, c.relname::text FROM
                pg_catalog.pg_inherits h
                JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
                JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
                WHERE h.inhparent = ''%I.%I''::regclass
                AND pg_get_expr(relpartbound, c.oid) = ''DEFAULT'''
            , v_parent_schemaname
            , v_parent_tablename);

        EXECUTE v_sql INTO v_default_schemaname, v_default_tablename;

        IF v_default_schemaname IS NOT NULL AND v_default_tablename IS NOT NULL THEN

            IF p_exact_count THEN
                v_sql := format('SELECT count(1) AS n FROM ONLY %I.%I', v_default_schemaname, v_default_tablename);
            ELSE
                v_sql := format('SELECT count(1) AS n FROM (SELECT 1 FROM ONLY %I.%I LIMIT 1) x', v_default_schemaname, v_default_tablename);
            END IF;

            EXECUTE v_sql INTO v_count;

            IF v_count > 0 THEN 
                v_trouble.default_table := v_default_schemaname ||'.'|| v_default_tablename;
                v_trouble.count := v_count;
                RETURN NEXT v_trouble;
            END IF;

        END IF;

    END IF; 

    v_count := 0;

END LOOP;

RETURN;

END
$$;

