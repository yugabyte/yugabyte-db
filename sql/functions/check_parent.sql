/*
 * Function to monitor for data getting inserted into parent tables managed by extension
 */
CREATE FUNCTION check_parent() RETURNS SETOF @extschema@.check_parent_table
    LANGUAGE plpgsql STABLE SECURITY DEFINER
    AS $$
DECLARE 
    
v_count 	bigint = 0;
v_sql       text;
v_tables 	record;
v_trouble   @extschema@.check_parent_table%rowtype;

BEGIN

FOR v_tables IN 
    SELECT DISTINCT parent_table FROM @extschema@.part_config
LOOP

    v_sql := 'SELECT count(1) AS n FROM ONLY '||v_tables.parent_table;
    EXECUTE v_sql INTO v_count;

    IF v_count > 0 THEN 
        v_trouble.parent_table := v_tables.parent_table;
        v_trouble.count := v_count;
        RETURN NEXT v_trouble;
    END IF;

	v_count := 0;

END LOOP;

RETURN;

END
$$;
