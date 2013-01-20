/*
 * Function to apply grants on parent & child tables
 */
CREATE FUNCTION apply_grants(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_child_table   text;
v_grants        text;
v_roles         text;
v_row           record;

BEGIN

FOR v_row IN 
    SELECT grants, roles FROM @extschema@.part_grants WHERE parent_table = p_parent_table
LOOP
    EXECUTE 'GRANT '||v_row.grants||' ON '||p_parent_table||' TO '||v_row.roles;
    FOR v_child_table IN 
        SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
    LOOP
        EXECUTE 'GRANT '||v_row.grants||' ON '||v_child_table||' TO '||v_row.roles;
    END LOOP;
END LOOP;

END
$$;
