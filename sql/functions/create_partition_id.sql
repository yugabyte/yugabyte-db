/*
 * Function to create id partitions
 */
CREATE FUNCTION create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true) RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze           boolean := FALSE;
v_control           text;
v_grantees          text[];
v_hasoids           boolean;
v_id                bigint;
v_id_position       int;
v_inherit_fk        boolean;
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_grant      record;
v_parent_owner      text;
v_parent_schema     text;
v_parent_tablename  text;
v_parent_tablespace text;
v_part_interval     bigint;
v_partition_created boolean := false;
v_partition_name    text;
v_revoke            text[];
v_row               record;
v_sql               text;
v_step_id           bigint;
v_sub_id_max        bigint;
v_sub_id_min        bigint;
v_tablename         text;
v_top_interval      bigint;
v_top_parent        text;
v_unlogged          char;

BEGIN

SELECT control
    , part_interval
    , inherit_fk
    , jobmon
INTO v_control
    , v_part_interval
    , v_inherit_fk
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

-- Check if parent table is a subpartition of an already existing id based partition set managed by pg_partman
-- If so, limit what child tables can be created based on parent suffix
WITH top_oid AS (
    SELECT i.inhparent AS top_parent_oid
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_inherits i ON c.oid = i.inhrelid
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname||'.'||c.relname = p_parent_table
) SELECT n.nspname||'.'||c.relname 
  INTO v_top_parent 
  FROM pg_catalog.pg_class c
  JOIN top_oid t ON c.oid = t.top_parent_oid
  JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
  JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
  WHERE c.oid = t.top_parent_oid
  AND p.type = 'id-static' OR p.type = 'id-dynamic';

IF v_top_parent IS NOT NULL THEN 
    SELECT part_interval::bigint INTO v_top_interval FROM @extschema@.part_config WHERE parent_table = v_top_parent;
    v_id_position := (length(p_parent_table) - position('p_' in reverse(p_parent_table))) + 2;
    v_sub_id_min = substring(p_parent_table from v_id_position)::bigint;
    v_sub_id_max = (v_sub_id_min + v_top_interval) - 1;
END IF;

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP
-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_id_min IS NOT NULL THEN
        IF v_id < v_sub_id_min OR v_id > v_sub_id_max THEN
            CONTINUE;
        END IF;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_id::text, TRUE);
    -- If child table already exists, skip creation
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_part_interval)-1);
    END IF;

    SELECT relpersistence INTO v_unlogged FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || ' TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT relhasoids INTO v_hasoids FROM pg_catalog.pg_class WHERE oid::regclass = p_parent_table::regclass;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE 'ALTER TABLE '||v_partition_name||' SET TABLESPACE '||v_parent_tablespace;
    END IF;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||v_control||'>='||quote_literal(v_id)||' AND '||v_control||'<'||quote_literal(v_id + v_part_interval)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(quote_ident(v_parent_schema)||'.'||quote_ident(v_parent_tablename), v_partition_name);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_control
            , sub_type
            , sub_part_interval
            , sub_constraint_cols
            , sub_premake
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_jobmon
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Subpartitioning '||v_partition_name);
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_use_run_maintenance := %L
                , p_inherit_fk := %L
                , p_jobmon := %L )'
            , v_partition_name
            , v_row.sub_control
            , v_row.sub_type
            , v_row.sub_part_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
        WHERE parent_table = v_partition_name;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

    END LOOP; -- end sub partitioning LOOP

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Analyzing partition set: '||p_parent_table);
    END IF;

    EXECUTE 'ANALYZE '||p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, 'No partitions created for partition set: '||p_parent_table);
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    PERFORM close_job(v_job_id);
END IF;
 
IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE TABLE: '||p_parent_table||''')' INTO v_job_id;
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

