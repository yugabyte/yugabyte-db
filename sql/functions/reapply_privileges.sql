/*
 * Function to re-apply ownership & privileges on all child tables in a partition set using parent table as reference
 */
CREATE FUNCTION reapply_privileges(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_owner       text;
v_child_grant       record;
v_grant             text;
v_grantees          text[];
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_match             boolean;
v_old_search_path   text;
v_parent_owner      text;
v_parent_schema     text;
v_parent_tablename  text;
v_owner_sql         text;
v_revoke            text;
v_row               record;
v_row_revoke        record;
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
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s', p_parent_table));
    v_step_id := add_step(v_job_id, 'Setting new child table privileges');
END IF;

SELECT schemaname, tablename, tableowner INTO v_parent_schema, v_parent_tablename, v_parent_owner FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'PENDING', format('Currently on child partition in ascending order: %s.%s'
                                                        , v_row.partition_schemaname
                                                        , v_row.partition_tablename));
    END IF;
    v_grantees := NULL;
    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
                , grantee
        FROM information_schema.table_privileges 
        WHERE table_schema = v_parent_schema AND table_name = v_parent_tablename
        GROUP BY grantee 
    LOOP
        -- Compare parent & child grants. Don't re-apply if it already exists
        v_match := false;
        FOR v_child_grant IN 
            SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
                    , grantee
            FROM information_schema.table_privileges 
            WHERE table_schema = v_row.partition_schemaname AND table_name = v_row.partition_tablename
            GROUP BY grantee 
        LOOP
            IF v_parent_grant.types = v_child_grant.types AND v_parent_grant.grantee = v_child_grant.grantee THEN
                v_match := true;
            END IF;
        END LOOP;

        IF v_match = false THEN
            EXECUTE format('GRANT %s ON %I.%I TO %I'
                            , array_to_string(v_parent_grant.types, ',')
                            , v_row.partition_schemaname
                            , v_row.partition_tablename
                            , v_parent_grant.grantee);
            SELECT string_agg(r, ',') INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
            IF v_revoke IS NOT NULL THEN
                EXECUTE format('REVOKE %s ON %I.%I FROM %I CASCADE'
                            , v_revoke
                            , v_row.partition_schemaname
                            , v_row.partition_tablename
                            , v_parent_grant.grantee);
            END IF;
        END IF;

        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);

    END LOOP;
    
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        FOR v_row_revoke IN 
            SELECT role FROM (
                SELECT DISTINCT grantee::text AS role FROM information_schema.table_privileges WHERE table_schema = v_row.partition_schemaname AND table_name = v_row.partition_tablename
                EXCEPT
                SELECT unnest(v_grantees)) x
        LOOP
            IF v_row_revoke.role IS NOT NULL THEN
                EXECUTE format('REVOKE ALL ON %I.%I FROM %I'
                            , v_row.partition_schemaname
                            , v_row.partition_tablename
                            , v_row_revoke.role);
            END IF;
        END LOOP;

    END IF;

    SELECT tableowner INTO v_child_owner FROM pg_tables WHERE schemaname = v_row.partition_schemaname AND tablename = v_row.partition_tablename;
    IF v_parent_owner <> v_child_owner THEN
        EXECUTE format('ALTER TABLE %I.%I OWNER TO %I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_parent_owner);
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    PERFORM close_job(v_job_id);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
END IF;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before first step logged'')', v_jobmon_schema, v_job_id) INTO v_step_id;
            END IF;
            EXECUTE format('SELECT %I.update_step(%s, ''CRITICAL'', %L)', v_jobmon_schema, v_step_id, 'ERROR: '||coalesce(SQLERRM,'unknown'));
            EXECUTE format('SELECT %I.fail_job(%s)', v_jobmon_schema, v_job_id);
        END IF;
        RAISE EXCEPTION '%
CONTEXT: %
DETAIL: %
HINT: %', ex_message, ex_context, ex_detail, ex_hint;
END
$$;

