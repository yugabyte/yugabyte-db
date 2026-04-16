-- Foreign keys placed on the parent table are now inherited to child tables.
    -- Any new partitions created after this update is installed will have the FKs applied to children.
    -- Existing child partitions will not have FKs applied. See the reapply_foreign_keys.py python script to reapply FKs to all child tables.
    -- The new apply_foreign_keys() function & reapply_foreign_keys.py script can be applied to any table inheritance set, not just the ones managed by pg_partman.
    -- See blog post for some more information about partitioning & foreign keys - http://www.keithf4.com/table-partitioning-and-foreign-keys
-- Unlogged table property on parent table is now automatically inherited to child tables. Note this only applies to newly created child partitions after this update is installed.
-- Added lockwait options to the undo partition functions (plpgsql & python)
-- Added the same autovacuum feature & option to undo_partition.py that partition_data.py has.
-- Made python scripts compatible with python 3
-- PgTAP tests for unlogged tables and FK inheritance

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.undo_partition_id(text, int, bigint, boolean, numeric) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition_id'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.undo_partition_time(text, int, interval, boolean, numeric) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition_time'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.undo_partition(text, int, boolean, boolean, numeric) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition'; 

DROP FUNCTION undo_partition_id(text, int, bigint, boolean);
DROP FUNCTION undo_partition_time(text, int, interval, boolean);
DROP FUNCTION undo_partition(text, int, boolean, boolean);

/*
 * Apply foreign keys that exist on the given parent to the given child table
 */
CREATE FUNCTION apply_foreign_keys(p_parent_table text, p_child_table text DEFAULT NULL, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_job_id            bigint;
v_jobmon            text;
v_jobmon_schema     text;
v_old_search_path   text;
v_ref_schema        text;
v_ref_table         text;
v_row               record;
v_schemaname        text;
v_sql               text;
v_step_id           bigint;
v_tablename         text;

BEGIN

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN APPLYING FOREIGN KEYS: '||p_parent_table);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Checking if target child table exists');
END IF;

SELECT schemaname, tablename INTO v_schemaname, v_tablename 
FROM pg_catalog.pg_tables 
WHERE schemaname||'.'||tablename = p_child_table;

IF v_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'CRITICAL', 'Target child table ('||v_child_table||') does not exist.');
        PERFORM fail_job(v_job_id);
        EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
    END IF;
    RAISE EXCEPTION 'Target child table (%.%) does not exist.', v_schemaname, v_tablename;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOR v_row IN 
    SELECT keys.conname
        , keys.confrelid::regclass::text AS ref_table
        , '"'||string_agg(att.attname, '","')||'"' AS ref_column
        , '"'||string_agg(att2.attname, '","')||'"' AS child_column
    FROM
        ( SELECT con.conname
                , unnest(con.conkey) as ref
                , unnest(con.confkey) as child
                , con.confrelid
                , con.conrelid
          FROM pg_catalog.pg_class c
          JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
          JOIN pg_catalog.pg_constraint con ON c.oid = con.conrelid
          WHERE n.nspname ||'.'|| c.relname = p_parent_table
          AND con.contype = 'f'
          ORDER BY con.conkey
    ) keys
    JOIN pg_catalog.pg_class cl ON cl.oid = keys.confrelid
    JOIN pg_catalog.pg_attribute att ON att.attrelid = keys.confrelid AND att.attnum = keys.child
    JOIN pg_catalog.pg_attribute att2 ON att2.attrelid = keys.conrelid AND att2.attnum = keys.ref
    GROUP BY keys.conname, keys.confrelid
LOOP
    SELECT schemaname, tablename INTO v_ref_schema, v_ref_table FROM pg_tables WHERE schemaname||'.'||tablename = v_row.ref_table;
    v_sql := format('ALTER TABLE %I.%I ADD FOREIGN KEY (%s) REFERENCES %I.%I (%s)', 
        v_schemaname, v_tablename, v_row.child_column, v_ref_schema, v_ref_table, v_row.ref_column);

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying FK: '||v_sql);
    END IF;

    EXECUTE v_sql;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'FK applied');
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN APPLYING FOREIGN KEYS: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to create id partitions
 */
CREATE OR REPLACE FUNCTION create_id_partition (p_parent_table text, p_partition_ids bigint[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze           boolean := FALSE;
v_control           text;
v_grantees          text[];
v_hasoids           boolean;
v_id                bigint;
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
v_partition_name    text;
v_revoke            text[];
v_sql               text;
v_step_id           bigint;
v_tablename         text;
v_unlogged          char;

BEGIN

SELECT control
    , part_interval
    , jobmon
INTO v_control
    , v_part_interval
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

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_id IN ARRAY p_partition_ids LOOP
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_id::text, TRUE);

    -- If child table already exists, skip creation
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
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

    PERFORM @extschema@.apply_foreign_keys(quote_ident(v_parent_schema)||'.'||quote_ident(v_parent_tablename), v_partition_name);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_analyze THEN
    EXECUTE 'ANALYZE '||p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

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


/*
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_partition_times timestamp[]) 
RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_grantees                      text[];
v_hasoids                       boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_part_interval                 interval;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_tablename                     text;
v_trunc_value                   text;
v_time                          timestamp;
v_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN

SELECT type
    , control
    , part_interval
    , jobmon
INTO v_type
    , v_control
    , v_part_interval
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');

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

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    v_partition_suffix := to_char(v_time, 'YYYY');
    IF v_part_interval < '1 year' AND v_part_interval <> '1 week' THEN 
        v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'MM');
        IF v_part_interval < '1 month' AND v_part_interval <> '1 week' THEN 
            v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'DD');
            IF v_part_interval < '1 day' THEN
                v_partition_suffix := v_partition_suffix || '_' || to_char(v_time, 'HH24MI');
                IF v_part_interval < '1 minute' THEN
                    v_partition_suffix := v_partition_suffix || to_char(v_time, 'SS');
                END IF; -- end < minute IF
            END IF; -- end < day IF      
        END IF; -- end < month IF
    END IF; -- end < year IF

    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_part_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');
        CONTINUE;
    END;

    IF v_part_interval = '1 week' THEN
        v_partition_suffix := to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
    END IF;
 
    -- "Q" is ignored in to_timestamp, so handle special case
    IF v_part_interval = '3 months' AND (v_type = 'time-static' OR v_type = 'time-dynamic') THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_suffix := v_year || 'q' || v_quarter;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
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
        CHECK ('||v_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||v_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    -- If custom time, set extra config options.
    IF v_type = 'time-custom' THEN
        INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
        VALUES ( p_parent_table, v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
    END IF;

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

    PERFORM @extschema@.apply_foreign_keys(quote_ident(v_parent_schema)||'.'||quote_ident(v_parent_tablename), v_partition_name);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        IF v_step_overflow_id IS NOT NULL THEN
            PERFORM fail_job(v_job_id);
        ELSE
            PERFORM close_job(v_job_id);
        END IF;
    END IF;

END LOOP;

IF v_analyze THEN
    EXECUTE 'ANALYZE '||p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

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


/*
 * Function to undo id-based partitioning created by this extension
 */
CREATE FUNCTION undo_partition_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval bigint DEFAULT NULL, p_keep_table boolean DEFAULT true, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_min             bigint;
v_child_table           text;
v_control               text;
v_exists                int;
v_function_name         text;
v_inner_loop_count      int;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_move_sql              text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         bigint;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_trig_name             text;
v_total                 bigint := 0;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_id_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_id_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::bigint
    , control
    , jobmon
INTO v_part_interval
    , v_control
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'id-static' OR type = 'id-dynamic');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

SELECT tgname INTO v_trig_name FROM pg_catalog.pg_trigger t WHERE tgname = v_trig_name;
IF v_trig_name IS NOT NULL THEN
    -- lockwait for trigger drop
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE 'LOCK TABLE ONLY '||p_parent_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
            RAISE NOTICE 'Unable to obtain lock on parent table to remove trigger';
            RETURN -1;
        END IF;
    END IF; -- END p_lock_wait IF
    EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
END IF; -- END trigger IF
v_lock_obtained := FALSE; -- reset for reuse later

EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE 'LOCK TABLE ONLY '||v_child_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
                RAISE NOTICE 'Unable to obtain lock on child table for removal from partition set';
                RETURN -1;
            END IF;
        END IF; -- END p_lock_wait IF
        v_lock_obtained := FALSE; -- reset for reuse later

        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE outer_child_loop;
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        -- lockwait timeout for row batches
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE 'SELECT * FROM ' || v_child_table ||
                    ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))
                    ||' FOR UPDATE NOWAIT';
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
               RAISE NOTICE 'Unable to obtain lock on batch of rows to move';
               RETURN -1;
            END IF;
        END IF;

        -- Get everything from the current child minimum up to the multiples of the given interval
        v_move_sql := 'WITH move_data AS (DELETE FROM '||v_child_table||
                ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))||' RETURNING *)
            INSERT INTO '||p_parent_table||' SELECT * FROM move_data';
        EXECUTE v_move_sql;
        GET DIAGNOSTICS v_rowcount = ROW_COUNT;
        v_total := v_total + v_rowcount;
        v_child_loop_total := v_child_loop_total + v_rowcount;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Moved '||v_child_loop_total||' rows to parent.');
        END IF;
        EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
        v_inner_loop_count := v_inner_loop_count + 1;
        v_batch_loop_count := v_batch_loop_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
    END LOOP inner_child_loop;
END LOOP outer_child_loop;

IF v_batch_loop_count < p_batch_count THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) to the parent. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) to the parent. Removed '||v_undo_count||' partitions.');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN UNDO PARTITIONING: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to undo time-based partitioning created by this extension
 */
CREATE FUNCTION undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_min             timestamptz;
v_child_loop_total      bigint := 0;
v_child_table           text;
v_control               text;
v_function_name         text;
v_inner_loop_count      int;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_move_sql              text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         interval;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_time_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_time_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::interval
    , control
    , jobmon
INTO v_part_interval
    , v_control
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

SELECT tgname INTO v_trig_name FROM pg_catalog.pg_trigger t WHERE tgname = v_trig_name;
IF v_trig_name IS NOT NULL THEN
    -- lockwait for trigger drop
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE 'LOCK TABLE ONLY '||p_parent_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
            RAISE NOTICE 'Unable to obtain lock on parent table to remove trigger';
            RETURN -1;
        END IF;
    END IF; -- END p_lock_wait IF
    EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
END IF; -- END trigger IF
v_lock_obtained := FALSE; -- reset for reuse later

EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE 'LOCK TABLE ONLY '||v_child_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
                RAISE NOTICE 'Unable to obtain lock on child table for removal from partition set';
                RETURN -1;
            END IF;
        END IF; -- END p_lock_wait IF
        v_lock_obtained := FALSE; -- reset for reuse later

        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Moved '||v_child_loop_total||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE outer_child_loop;
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        -- do some locking with timeout, if required
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE 'SELECT * FROM ' || v_child_table ||
                    ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))
                    ||' FOR UPDATE NOWAIT';
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
               RAISE NOTICE 'Unable to obtain lock on batch of rows to move';
               RETURN -1;
            END IF;
        END IF;

        -- Get everything from the current child minimum up to the multiples of the given interval
        v_move_sql := 'WITH move_data AS (DELETE FROM '||v_child_table||
                ' WHERE '||v_control||' <= '||quote_literal(v_child_min + (p_batch_interval * v_inner_loop_count))||' RETURNING *)
            INSERT INTO '||p_parent_table||' SELECT * FROM move_data';
        EXECUTE v_move_sql;
        GET DIAGNOSTICS v_rowcount = ROW_COUNT;
        v_total := v_total + v_rowcount;
        v_child_loop_total := v_child_loop_total + v_rowcount;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Moved '||v_child_loop_total||' rows to parent.');
        END IF;
        EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
        v_inner_loop_count := v_inner_loop_count + 1;
        v_batch_loop_count := v_batch_loop_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
    END LOOP inner_child_loop;
END LOOP outer_child_loop;

IF v_batch_loop_count < p_batch_count THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) to the parent. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) to the parent. Removed '||v_undo_count||' partitions.');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN UNDO PARTITIONING: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to undo partitioning. 
 * Will actually work on any parent/child table set, not just ones created by pg_partman.
 */
CREATE FUNCTION undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true, p_jobmon boolean DEFAULT true, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      bigint := 0;
v_child_count           bigint;
v_child_table           text;
v_copy_sql              text;
v_function_name         text;
v_job_id                bigint;
v_jobmon_schema         text;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_part_interval         interval;
v_rowcount              bigint;
v_step_id               bigint;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition already running.';
    RETURN 0;
END IF;

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

SELECT tgname INTO v_trig_name FROM pg_catalog.pg_trigger t WHERE tgname = v_trig_name;
IF v_trig_name IS NOT NULL THEN
    -- lockwait for trigger drop
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE 'LOCK TABLE ONLY '||p_parent_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
            RAISE NOTICE 'Unable to obtain lock on parent table to remove trigger';
            RETURN -1;
        END IF;
    END IF; -- END p_lock_wait IF
    EXECUTE 'DROP TRIGGER IF EXISTS '||v_trig_name||' ON '||p_parent_table;
END IF; -- END trigger IF
v_lock_obtained := FALSE; -- reset for reuse later

EXECUTE 'DROP FUNCTION IF EXISTS '||v_function_name||'()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT n.nspname||'.'||c.relname INTO v_child_table
    FROM pg_inherits i 
    JOIN pg_class c ON i.inhrelid = c.oid 
    JOIN pg_namespace n ON c.relnamespace = n.oid 
    WHERE i.inhparent::regclass = p_parent_table::regclass 
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    EXECUTE 'SELECT count(*) FROM '||v_child_table INTO v_child_count;
    IF v_child_count = 0 THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE 'LOCK TABLE ONLY '||v_child_table||' IN ACCESS EXCLUSIVE MODE NOWAIT';
                    v_lock_obtained := TRUE;
                EXCEPTION
                    WHEN lock_not_available THEN
                        PERFORM pg_sleep( p_lock_wait / 5.0 );
                        CONTINUE;
                END;
                EXIT WHEN v_lock_obtained;
            END LOOP;
            IF NOT v_lock_obtained THEN
                RAISE NOTICE 'Unable to obtain lock on child table for removal from partition set';
                RETURN -1;
            END IF;
        END IF; -- END p_lock_wait IF
        v_lock_obtained := FALSE; -- reset for reuse later

        EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
        IF p_keep_table = false THEN
            EXECUTE 'DROP TABLE '||v_child_table;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||coalesce(v_rowcount, 0)||' rows to parent');
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Copied '||coalesce(v_rowcount, 0)||' rows to parent');
            END IF;
        END IF;
        v_undo_count := v_undo_count + 1;
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;
   
    -- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE 'SELECT * FROM '|| v_child_table ||' FOR UPDATE NOWAIT';
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RAISE NOTICE 'Unable to obtain lock on batch of rows to move';
           RETURN -1;
        END IF;
    END IF;

    v_copy_sql := 'INSERT INTO '||p_parent_table||' SELECT * FROM '||v_child_table;
    EXECUTE v_copy_sql;
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total := v_total + v_rowcount;

    EXECUTE 'ALTER TABLE '||v_child_table||' NO INHERIT ' || p_parent_table;
    IF p_keep_table = false THEN
        EXECUTE 'DROP TABLE '||v_child_table;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Child table DROPPED. Moved '||v_rowcount||' rows to parent');
        END IF;
    ELSE
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Child table UNINHERITED, not DROPPED. Copied '||v_rowcount||' rows to parent');
        END IF;
    END IF;
    v_batch_loop_count := v_batch_loop_count + 1;
    v_undo_count := v_undo_count + 1;         
END LOOP;

IF v_undo_count = 0 THEN
    -- FOR loop never ran, so there's no child tables left.
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman (if it existed)');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) from % child table(s) to the parent: %', v_total, v_undo_count, p_parent_table;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', 'Copied '||v_total||' row(s) from '||v_undo_count||' child table(s) to the parent');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN UNDO PARTITIONING: '||p_parent_table||''')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition function maintenance for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;

-- Restore dropped object privileges
DO $$
DECLARE
v_row   record;
BEGIN
    FOR v_row IN SELECT statement FROM partman_preserve_privs_temp LOOP
        IF v_row.statement IS NOT NULL THEN
            EXECUTE v_row.statement;
        END IF;
    END LOOP;
END
$$;

DROP TABLE IF EXISTS partman_preserve_privs_temp;
