/*
 * Function to monitor for data getting inserted into parent tables managed by extension
 */
CREATE FUNCTION check_parent(p_exact_count boolean DEFAULT true) RETURNS SETOF @extschema@.check_parent_table
    LANGUAGE plpgsql STABLE SECURITY DEFINER 
    AS $$
DECLARE

v_count             bigint = 0;
v_new_search_path   text := '@extschema@,pg_temp';
v_old_search_path   text;
v_row               record;
v_schemaname        text;
v_tablename         text;
v_sql               text;
v_trouble           @extschema@.check_parent_table%rowtype;

BEGIN

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config
LOOP
    SELECT schemaname, tablename 
    INTO v_schemaname, v_tablename
    FROM pg_catalog.pg_tables 
    WHERE schemaname = split_part(v_row.parent_table, '.', 1)::name
    AND tablename = split_part(v_row.parent_table, '.', 2)::name;

    IF p_exact_count THEN
        v_sql := format('SELECT count(1) AS n FROM ONLY %I.%I', v_schemaname, v_tablename);
    ELSE
        v_sql := format('SELECT count(1) AS n FROM (SELECT 1 FROM ONLY %I.%I LIMIT 1) x', v_schemaname, v_tablename);
    END IF;
    EXECUTE v_sql INTO v_count;

    IF v_count > 0 THEN 
        v_trouble.parent_table := v_schemaname ||'.'|| v_tablename;
        v_trouble.count := v_count;
        RETURN NEXT v_trouble;
    END IF;

    v_count := 0;

END LOOP;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN;

END
$$;


