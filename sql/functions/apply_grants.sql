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

BEGIN

SELECT grants, roles INTO v_grants, v_roles FROM @extschema@.part_grants WHERE parent_table = p_parent_table;

IF v_grants IS NOT NULL AND v_roles IS NOT NULL THEN

    EXECUTE 'GRANT '||v_grants||' ON '||p_parent_table||' TO '||v_roles;

    FOR v_child_table IN 
        SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
    LOOP
        EXECUTE 'GRANT '||v_grants||' ON TABLE '||v_child_table||' TO '||v_roles;
    END LOOP;

END IF;

END
$$;
