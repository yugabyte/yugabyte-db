/*
 * Function to monitor for data getting inserted into parent tables managed by extension
 */
CREATE FUNCTION check_parent() RETURNS SETOF @extschema@.check_parent_table
    LANGUAGE plpgsql STABLE SECURITY DEFINER
    AS $$
DECLARE

v_count         bigint = 0;
v_row           record;
v_schemaname    text;
v_tablename     text;
v_sql           text;
v_trouble       @extschema@.check_parent_table%rowtype;

BEGIN

FOR v_row IN 
    SELECT DISTINCT parent_table FROM @extschema@.part_config
LOOP
    SELECT schemaname, tablename INTO v_schemaname, v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_row.parent_table;

    v_sql := format('SELECT count(1) AS n FROM ONLY %I.%I', v_schemaname, v_tablename);
    EXECUTE v_sql INTO v_count;

    IF v_count > 0 THEN 
        v_trouble.parent_table := v_schemaname ||'.'|| v_tablename;
        v_trouble.count := v_count;
        RETURN NEXT v_trouble;
    END IF;

    v_count := 0;

END LOOP;

RETURN;

END
$$;


