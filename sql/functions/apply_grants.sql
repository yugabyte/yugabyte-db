/*
 * Function to apply ownership & privileges on child tables using parent table as reference
 */
CREATE FUNCTION apply_grants(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_table   text;
v_count         int := 0;
v_grant         text;
v_grantees      text[];
v_owner         text;
v_owner_sql     text;
v_revoke        text[];
v_revoke_sql    text;
v_row           record;
v_sql           text;

BEGIN

SELECT count(parent_table) INTO v_count FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_count = 0  THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

SELECT tableowner INTO v_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOR v_child_table IN 
    SELECT inhrelid::regclass FROM pg_catalog.pg_inherits WHERE inhparent::regclass = p_parent_table::regclass ORDER BY inhrelid::regclass ASC
LOOP
    v_grantees := NULL;
    FOR v_row IN 
        SELECT array_agg(privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_row.types, ',')||' ON '||v_child_table||' TO '||v_row.grantee;

        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_row.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_child_table||' FROM '||v_row.grantee||' CASCADE';
        END IF;

        v_grantees := array_append(v_grantees, v_row.grantee::text);
    END LOOP;
    
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_child_table
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_child_table||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_child_table||' OWNER TO '||v_owner;

END LOOP;

END
$$;
