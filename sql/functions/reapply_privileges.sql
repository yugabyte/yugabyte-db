/*
 * Function to re-apply ownership & privileges on all child tables in a partition set using parent table as reference
 */
CREATE FUNCTION reapply_privileges(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_owner       text;
v_child_table       text;
v_child_grant       record;
v_grant             text;
v_grantees          text[];
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_match             boolean;
v_old_search_path   text;
v_parent_owner      text;
v_owner_sql         text;
v_revoke            text[];
v_parent_grant      record;
v_sql               text;
v_step_id           bigint;

BEGIN

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_jobmon IS NULL THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Setting new child table privileges');
END IF;

SELECT tableowner INTO v_parent_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOR v_child_table IN 
    SELECT n.nspname||'.'||c.relname FROM pg_inherits i join pg_class c ON i.inhrelid = c.oid join pg_namespace n ON c.relnamespace = n.oid WHERE i.inhparent::regclass = p_parent_table::regclass ORDER BY i.inhrelid ASC
LOOP
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'PENDING', 'Currently on child partition in ascending order: '||v_child_table);
    END IF;
    v_grantees := NULL;
    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        -- Compare parent & child grants. Don't re-apply if it already exists
        v_match := false;
        FOR v_child_grant IN 
            SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
            FROM information_schema.table_privileges 
            WHERE table_schema ||'.'|| table_name = v_child_table
            GROUP BY grantee 
        LOOP
            IF v_parent_grant.types = v_child_grant.types AND v_parent_grant.grantee = v_child_grant.grantee THEN
                v_match := true;
            END IF;
        END LOOP;

        IF v_match = false THEN
            EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_child_table||' TO '||v_parent_grant.grantee;
            SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
            IF v_revoke IS NOT NULL THEN
                EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_child_table||' FROM '||v_parent_grant.grantee||' CASCADE';
            END IF;
        END IF;

        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);

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

    SELECT tableowner INTO v_child_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
    IF v_parent_owner <> v_child_owner THEN
        EXECUTE 'ALTER TABLE '||v_child_table||' OWNER TO '||v_parent_owner;
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: '||p_parent_table||''')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before job logging started'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;

