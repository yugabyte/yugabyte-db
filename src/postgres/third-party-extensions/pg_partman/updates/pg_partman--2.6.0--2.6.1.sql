-- WARNING: The pg_partman background worker is currently incompatible with PostgreSQL 9.6. It runs fine in 9.4 and 9.5. I'm still working on trying to figure out what the issue is. If you must upgrade to 9.6 before this issue is fixed, you can disable the background worker by removing the entry for it in shared_preload_libraries and restarting postgres. Instead schedule the run_maintenance() plpgsql function to run via another scheduler method (cron).

-- NOTE: A restart of the database is required to put the background worker code changes in place once the new library file is installed.

-- Add p_analyze parameter to partition_data_id() and partition_data_time() functions to allow moving data out of the parent to be faster if an analyze is not required after each child table is created. If set to false, highly recommended that an analyze on the entire partitions set is run after moving data. (Github Pull Request #137)
    -- Also allows better control of automatic analyzing of new partitions for serial partition sets by allowing the p_analyze parameter to run_maintenance() to be passed further down the function call hierarchy.
    -- Updated partition_data.py to pass this parameter as FALSE when calling these functions. The script does a VACUUM ANALYZE when it completes all batches anyway, so an analyze per child table is not necessary.
-- Improve performance during privilege inheritance when creating new partitions or when running reapply_privileges() function. Most noticable on databases with many objects (Github Issue #139).
-- Fixed buffer overflow in background worker code if database name was too long (Github Pull Request #140).
-- Fixed bug in background worker code that could cause improper initialization of dynamic bgw processes (Github Pull Request #140).
-- Account for non-lowercase schemename for pg_partman's schema in background worker (Github PullRequest #140).


CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.partition_data_id(text, int, bigint, numeric, text, boolean)  TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'partition_data_id'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_function_id(text, bigint, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_function_id'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.partition_data_time(text, int, interval, numeric, text, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'partition_data_time'; 

DROP FUNCTION partition_data_id(text, int, bigint, numeric, text);
DROP FUNCTION partition_data_time(text, int, interval, numeric, text);
DROP FUNCTION create_function_id(text, bigint);


/*
 * Custom view to help improve privilege lookups for pg_partman. 
 * information_schema is a performance bottleneck since indexes aren't being used properly.
 */
CREATE VIEW @extschema@.table_privs AS
    SELECT u_grantor.rolname AS grantor,
           grantee.rolname AS grantee,
           nc.nspname AS table_schema,
           c.relname AS table_name,
           c.prtype AS privilege_type
    FROM (
            SELECT oid, relname, relnamespace, relkind, relowner, (aclexplode(coalesce(relacl, acldefault('r', relowner)))).* FROM pg_class
         ) AS c (oid, relname, relnamespace, relkind, relowner, grantor, grantee, prtype, grantable),
         pg_namespace nc,
         pg_authid u_grantor,
         (
           SELECT oid, rolname FROM pg_authid
           UNION ALL
           SELECT 0::oid, 'PUBLIC'
         ) AS grantee (oid, rolname)
    WHERE c.relnamespace = nc.oid
          AND c.relkind IN ('r', 'v')
          AND c.grantee = grantee.oid
          AND c.grantor = u_grantor.oid
          AND c.prtype IN ('INSERT', 'SELECT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER')
          AND (pg_has_role(u_grantor.oid, 'USAGE')
               OR pg_has_role(grantee.oid, 'USAGE')
               OR grantee.rolname = 'PUBLIC' );


/*
 * Apply privileges that exist on a given parent to the given child table
 */
CREATE OR REPLACE FUNCTION apply_privileges(p_parent_schema text, p_parent_tablename text, p_child_schema text, p_child_tablename text, p_job_id bigint DEFAULT NULL) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_grant       record;
v_child_owner       text;
v_grantees          text[];
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_match             boolean;
v_parent_grant      record;
v_parent_owner      text;
v_revoke            text;
v_row_revoke        record;
v_sql               text;
v_step_id           bigint;

BEGIN

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_schema ||'.'|| p_parent_tablename;
IF v_jobmon IS NULL THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %.%', p_parent_schema, p_parent_tablename;
END IF;

SELECT tableowner INTO v_parent_owner FROM pg_catalog.pg_tables WHERE schemaname = p_parent_schema::name AND tablename = p_parent_tablename::name;
SELECT tableowner INTO v_child_owner FROM pg_catalog.pg_tables WHERE schemaname = p_child_schema::name AND tablename = p_child_tablename::name;
IF v_parent_owner IS NULL THEN
    RAISE EXCEPTION 'Given parent table does not exist: %.%', v_parent_schema, v_parent_tablename;
END IF;
IF v_child_owner IS NULL THEN
    RAISE EXCEPTION 'Given child table does not exist: %.%', v_child_schema, v_child_tablename;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        EXECUTE format('SELECT %I.add_job(%L)', v_jobmon_schema, format('PARTMAN APPLYING PRIVILEGES TO CHILD TABLE: %s.%s', p_child_schema, p_child_tablename)) INTO v_job_id;
    ELSE
        v_job_id := p_job_id;
    END IF;
    EXECUTE format('SELECT %I.add_step(%L, %L)', v_jobmon_schema, v_job_id, format('Setting new child table privileges for %s.%s', p_child_schema, p_child_tablename)) INTO v_step_id;
END IF;

IF v_jobmon_schema IS NOT NULL THEN

    EXECUTE format('SELECT %I.update_step(%L, %L, %L)'
            , v_jobmon_schema
            , v_step_id
            , 'PENDING'
            , format('Applying privileges on child partition: %s.%s'
                , p_child_schema
                , p_child_tablename)
            );
END IF;

FOR v_parent_grant IN 
    SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
            , grantee
    FROM @extschema@.table_privs
    WHERE table_schema = p_parent_schema::name AND table_name = p_parent_tablename::name
    GROUP BY grantee 
LOOP
    -- Compare parent & child grants. Don't re-apply if it already exists
    v_match := false;
    v_sql := NULL;
    FOR v_child_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
                , grantee
        FROM @extschema@.table_privs 
        WHERE table_schema = p_child_schema::name AND table_name = p_child_tablename::name
        GROUP BY grantee 
    LOOP
        IF v_parent_grant.types = v_child_grant.types AND v_parent_grant.grantee = v_child_grant.grantee THEN
            v_match := true;
        END IF;
    END LOOP;

    IF v_match = false THEN
        IF v_parent_grant.grantee = 'PUBLIC' THEN
            v_sql := 'GRANT %s ON %I.%I TO %s';
        ELSE
            v_sql := 'GRANT %s ON %I.%I TO %I';
        END IF;
        EXECUTE format(v_sql
                        , array_to_string(v_parent_grant.types, ',')
                        , p_child_schema
                        , p_child_tablename
                        , v_parent_grant.grantee);
        v_sql := NULL;
        SELECT string_agg(r, ',') INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            IF v_parent_grant.grantee = 'PUBLIC' THEN
                v_sql := 'REVOKE %s ON %I.%I FROM %s CASCADE';
            ELSE
                v_sql := 'REVOKE %s ON %I.%I FROM %I CASCADE';
            END IF;
            EXECUTE format(v_sql
                        , v_revoke
                        , p_child_schema
                        , p_child_tablename
                        , v_parent_grant.grantee);
            v_sql := NULL;
        END IF;
    END IF;

    v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);

END LOOP;

-- Revoke all privileges from roles that have none on the parent
IF v_grantees IS NOT NULL THEN
    FOR v_row_revoke IN 
        SELECT role FROM (
            SELECT DISTINCT grantee::text AS role FROM @extschema@.table_privs WHERE table_schema = p_child_schema::name AND table_name = p_child_tablename::name
            EXCEPT
            SELECT unnest(v_grantees)) x
    LOOP
        IF v_row_revoke.role IS NOT NULL THEN
            IF v_row_revoke.role = 'PUBLIC' THEN
                v_sql := 'REVOKE ALL ON %I.%I FROM %s';
            ELSE
                v_sql := 'REVOKE ALL ON %I.%I FROM %I';
            END IF;
            EXECUTE format(v_sql
                        , p_child_schema
                        , p_child_tablename
                        , v_row_revoke.role);
        END IF;
    END LOOP;

END IF;

IF v_parent_owner <> v_child_owner THEN
    EXECUTE format('ALTER TABLE %I.%I OWNER TO %I'
                , p_child_schema
                , p_child_tablename
                , v_parent_owner);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.update_step(%L, %L, %L)', v_jobmon_schema, v_step_id, 'OK', 'Done');
    IF p_job_id IS NULL THEN
        EXECUTE format('SELECT %I.close_job(%L)', v_jobmon_schema, v_job_id);
    END IF;
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


/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE FUNCTION create_function_id(p_parent_table text, p_job_id bigint DEFAULT NULL, p_analyze boolean DEFAULT true) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_control                       text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_function_name                 text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_position                   int;
v_job_id                        bigint;
v_jobmon                        text;
v_jobmon_schema                 text;
v_last_partition                text;
v_max                           bigint;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_optimize_trigger              int;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_interval            bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_row_max_id                    record;
v_run_maint                     boolean;
v_step_id                       bigint;
v_top_parent                    text := p_parent_table;
v_trig_func                     text;
v_trigger_exception_handling    boolean;
v_trigger_return_null           boolean;
v_upsert                        text;

BEGIN

SELECT partition_interval::bigint
    , control
    , premake
    , optimize_trigger
    , use_run_maintenance
    , jobmon
    , trigger_exception_handling
    , upsert
    , trigger_return_null
INTO v_partition_interval
    , v_control
    , v_premake
    , v_optimize_trigger
    , v_run_maint
    , v_jobmon
    , v_trigger_exception_handling
    , v_upsert
    , v_trigger_return_null
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND partition_type = 'id';

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE FUNCTION: %s', p_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
    v_step_id := add_step(v_job_id, format('Creating partition function for table %s', p_parent_table));
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;
v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

-- Get the highest level top parent if multi-level partitioned in order to get proper max() value below
WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
    WITH top_oid AS (
        SELECT i.inhparent AS top_parent_oid
        FROM pg_catalog.pg_inherits i
        JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = v_higher_parent_schema::name
        AND c.relname = v_higher_parent_table::name
    ) SELECT n.nspname, c.relname
    INTO v_higher_parent_schema, v_higher_parent_table
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    JOIN top_oid t ON c.oid = t.top_parent_oid
    JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
    WHERE p.partition_type = 'id';

    IF v_higher_parent_table IS NOT NULL THEN
        -- initially set in DECLARE
        v_top_parent := v_higher_parent_schema||'.'||v_higher_parent_table;
    END IF;

END LOOP;

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_top_parent, 'DESC')
LOOP
        EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max_id.partition_schemaname, v_row_max_id.partition_tablename) INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;
IF v_max IS NULL THEN
    v_max := 0;
END IF;
v_current_partition_id = v_max - (v_max % v_partition_interval);
v_next_partition_id := v_current_partition_id + v_partition_interval;
v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_current_partition_id::text, TRUE);

v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$ 
    DECLARE
        v_count                     int;
        v_current_partition_id      bigint;
        v_current_partition_name    text;
        v_id_position               int;
        v_last_partition            text := %L;
        v_next_partition_id         bigint;
        v_next_partition_name       text;
        v_partition_created         boolean;
    BEGIN
    IF TG_OP = ''INSERT'' THEN 
        IF NEW.%I >= %s AND NEW.%I < %s THEN '
            , v_parent_schema
            , v_function_name
            , v_last_partition
            , v_control
            , v_current_partition_id
            , v_control
            , v_next_partition_id
        );
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_current_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func || format(' 
            INSERT INTO %I.%I VALUES (NEW.*) %s; ', v_parent_schema, v_current_partition_name, v_upsert);
        ELSE
            v_trig_func := v_trig_func || '
            -- Child table for current values does not exist in this partition set, so write to parent
            RETURN NEW;';
        END IF;

    FOR i IN 1..v_optimize_trigger LOOP
        v_prev_partition_id := v_current_partition_id - (v_partition_interval * i);
        v_next_partition_id := v_current_partition_id + (v_partition_interval * i);
        v_final_partition_id := v_next_partition_id + v_partition_interval;
        v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_prev_partition_id::text, TRUE);
        v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_next_partition_id::text, TRUE);

        -- Check that child table exist before making a rule to insert to them.
        -- Handles optimize_trigger being larger than premake (to go back in time further) and edge case of changing optimize_trigger immediately after running create_parent().
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_prev_partition_name::name;
        IF v_count > 0 THEN
            -- Only handle previous partitions if they're starting above zero
            IF v_prev_partition_id >= 0 THEN
                v_trig_func := v_trig_func ||format('
        ELSIF NEW.%I >= %s AND NEW.%I < %s THEN 
            INSERT INTO %I.%I VALUES (NEW.*) %s; '
                , v_control
                , v_prev_partition_id
                , v_control
                , v_prev_partition_id + v_partition_interval
                , v_parent_schema
                , v_prev_partition_name
                , v_upsert
            );
            END IF;
        END IF;

        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_next_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format('
        ELSIF NEW.%I >= %s AND NEW.%I < %s THEN 
            INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_control
                , v_next_partition_id
                , v_control
                , v_final_partition_id
                , v_parent_schema
                , v_next_partition_name
                , v_upsert
            );
        END IF;
    END LOOP;
    v_trig_func := v_trig_func ||format('
        ELSE
            v_current_partition_id := NEW.%I - (NEW.%I %% %s);
            v_current_partition_name := @extschema@.check_name_length(%L, v_current_partition_id::text, TRUE);
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = %L::name AND tablename = v_current_partition_name::name;
            IF v_count > 0 THEN 
                EXECUTE format(''INSERT INTO %%I.%%I VALUES($1.*) %s'', %L, v_current_partition_name) USING NEW;
            ELSE
                RETURN NEW;
            END IF;
        END IF;'
            , v_control
            , v_control
            , v_partition_interval
            , v_parent_tablename
            , v_parent_schema
            , v_upsert
            , v_parent_schema
        );

    IF v_run_maint IS FALSE THEN
        v_trig_func := v_trig_func ||format('
        v_current_partition_id := NEW.%I - (NEW.%I %% %s);
        IF (NEW.%I %% %s) > (%s / 2) THEN
            v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
            v_next_partition_id := (substring(v_last_partition from v_id_position)::bigint) + %s;
            WHILE ((v_next_partition_id - v_current_partition_id) / %s) <= %s LOOP 
                v_partition_created := @extschema@.create_partition_id(%L, ARRAY[v_next_partition_id], p_analyze := %L);
                IF v_partition_created THEN
                    PERFORM @extschema@.create_function_id(%L, p_analyze := %L);
                    PERFORM @extschema@.apply_constraints(%L);
                END IF;
                v_next_partition_id := v_next_partition_id + %s;
            END LOOP;
        END IF;'
            , v_control
            , v_control
            , v_partition_interval
            , v_control
            , v_partition_interval
            , v_partition_interval
            , v_partition_interval
            , v_partition_interval
            , v_premake
            , p_parent_table
            , p_analyze
            , p_parent_table
            , p_analyze
            , p_parent_table
            , v_partition_interval
        );
    END IF;

    v_trig_func := v_trig_func ||'
    END IF;';

    IF v_trigger_return_null IS TRUE THEN
        v_trig_func := v_trig_func ||'
    RETURN NULL;';
    ELSE
        v_trig_func := v_trig_func ||'
    RETURN NEW;';
    END IF;

    IF v_trigger_exception_handling THEN 
        v_trig_func := v_trig_func ||'
    EXCEPTION WHEN OTHERS THEN
        RAISE WARNING ''pg_partman insert into child table failed, row inserted into parent (%.%). ERROR: %'', TG_TABLE_SCHEMA, TG_TABLE_NAME, COALESCE(SQLERRM, ''unknown'');
        RETURN NEW;';
    END IF;

    v_trig_func := v_trig_func ||'
    END $t$;';

EXECUTE v_trig_func;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Added function for current id interval: %s to %s', v_current_partition_id, v_final_partition_id-1));
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE FUNCTION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition function maintenance for table %s failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
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



/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_id(p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval bigint DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true) 
    RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_current_partition_name    text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_id          bigint;
v_min_partition_id          bigint;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_start_control             bigint;
v_total_rows                bigint := 0;

BEGIN

SELECT partition_interval::bigint
    , control
INTO v_partition_interval
    , v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND partition_type = 'id';
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        v_partition_id := ARRAY[v_min_partition_id];
        -- Check if custom batch interval overflows current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_id + v_partition_interval) THEN
            v_max_partition_id := v_min_partition_id + v_partition_interval;
        ELSE
            v_max_partition_id := v_start_control + p_batch_interval;
        END IF;

    ELSIF p_order = 'DESC' THEN
        EXECUTE 'SELECT max('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_partition_interval);
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_id := v_min_partition_id + v_partition_interval;
        v_partition_id := ARRAY[v_min_partition_id];
        -- Make sure minimum doesn't underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_id THEN
            v_min_partition_id = v_start_control - p_batch_interval;
        END IF;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    -- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                v_sql := format('SELECT * FROM ONLY %I.%I WHERE %I >= %s AND %I < %s FOR UPDATE NOWAIT'
                                , v_parent_schema
                                , v_parent_tablename
                                , v_control
                                , v_min_partition_id
                                , v_control
                                , v_max_partition_id);
                EXECUTE v_sql;
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RETURN -1;
        END IF;
    END IF;

    PERFORM @extschema@.create_partition_id(p_parent_table, v_partition_id, p_analyze);
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_min_partition_id::text, TRUE);

    EXECUTE format('WITH partition_data AS (
                        DELETE FROM ONLY %I.%I WHERE %I >= %s AND %I < %s RETURNING *)
                    INSERT INTO %I.%I SELECT * FROM partition_data'
                , v_parent_schema
                , v_parent_tablename
                , v_control
                , v_min_partition_id
                , v_control
                , v_max_partition_id
                , v_parent_schema
                , v_current_partition_name);

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP;

PERFORM @extschema@.create_function_id(p_parent_table, NULL, p_analyze);

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

END
$$;


/*
 * Function to turn a table into the parent of a partition set
 */
CREATE OR REPLACE FUNCTION create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_use_run_maintenance boolean DEFAULT NULL
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch boolean DEFAULT false
    , p_upsert text DEFAULT ''
    , p_trigger_return_null boolean DEFAULT true
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_base_timestamp                timestamptz;
v_count                         int := 1;
v_datetime_string               text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_interval                   bigint;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_created        boolean;
v_max                           bigint;
v_notnull                       boolean;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_partition_id           bigint;
v_parent_partition_timestamp    timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_time                timestamptz;
v_partition_time_array          timestamptz[];
v_partition_id_array            bigint[];
v_row                           record;
v_run_maint                     boolean;
v_sql                           text;
v_start_time                    timestamptz;
v_starting_partition_id         bigint;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_parent                    text;
v_success                       boolean := false;
v_time_interval                 interval;
v_top_datetime_string           text;
v_top_parent_schema             text := split_part(p_parent_table, '.', 1);
v_top_parent_table              text := split_part(p_parent_table, '.', 2);

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

IF p_upsert <> '' AND @extschema@.check_version('9.5.0') = 'false' THEN
    RAISE EXCEPTION 'INSERT ... ON CONFLICT (UPSERT) feature is only supported in PostgreSQL 9.5 and later';
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull 
FROM pg_catalog.pg_attribute a
JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE c.relname = v_parent_tablename::name
AND n.nspname = v_parent_schema::name
AND a.attname = p_control::name;
    IF v_notnull = false OR v_notnull IS NULL THEN
        RAISE EXCEPTION 'Control column given (%) for parent table (%) does not exist or must be set to NOT NULL', p_control, p_parent_table;
    END IF;

IF p_type = 'id' AND p_epoch = true THEN
    RAISE EXCEPTION 'p_epoch can only be used with time-based partitioning';
END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_use_run_maintenance IS NOT NULL THEN
    IF p_use_run_maintenance IS FALSE AND (p_type = 'time' OR p_type = 'time-custom') THEN
        RAISE EXCEPTION 'p_run_maintenance cannot be set to false for time based partitioning';
    END IF;
    v_run_maint := p_use_run_maintenance;
ELSIF p_type = 'time' OR p_type = 'time-custom' THEN
    v_run_maint := TRUE;
ELSIF p_type = 'id' THEN
    v_run_maint := FALSE;
ELSE
    RAISE EXCEPTION 'use_run_maintenance value cannot be set NULL';
END IF;

EXECUTE format('LOCK TABLE %I.%I IN ACCESS EXCLUSIVE MODE', v_parent_schema, v_parent_tablename);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN SETUP PARENT: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Creating initial partitions on new parent table: %s', p_parent_table));
END IF;

-- If this parent table has siblings that are also partitioned (subpartitions), ensure this parent gets added to part_config_sub table so future maintenance will subpartition it
-- Just doing in a loop to avoid having to assign a bunch of variables (should only run once, if at all; constraint should enforce only one value.)
FOR v_row IN 
    WITH parent_table AS (
        SELECT h.inhparent AS parent_oid
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON h.inhrelid = c.oid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename::name
        AND n.nspname = v_parent_schema::name
    ), sibling_children AS (
        SELECT i.inhrelid::regclass::text AS tablename 
        FROM pg_inherits i
        JOIN parent_table p ON i.inhparent = p.parent_oid
    )
    SELECT DISTINCT sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_use_run_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null
    FROM @extschema@.part_config_sub a
    JOIN sibling_children b on a.sub_parent = b.tablename LIMIT 1
LOOP
    INSERT INTO @extschema@.part_config_sub (
        sub_parent
        , sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_use_run_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null)
    VALUES (
        p_parent_table
        , v_row.sub_partition_type
        , v_row.sub_control
        , v_row.sub_partition_interval
        , v_row.sub_constraint_cols
        , v_row.sub_premake
        , v_row.sub_inherit_fk
        , v_row.sub_retention
        , v_row.sub_retention_schema
        , v_row.sub_retention_keep_table
        , v_row.sub_retention_keep_index
        , v_row.sub_use_run_maintenance
        , v_row.sub_epoch
        , v_row.sub_optimize_trigger
        , v_row.sub_optimize_constraint
        , v_row.sub_infinite_time_partitions
        , v_row.sub_jobmon
        , v_row.sub_trigger_exception_handling
        , v_row.sub_upsert
        , v_row.sub_trigger_return_null);
END LOOP;

IF p_type = 'time' OR p_type = 'time-custom' THEN

    CASE
        WHEN p_interval = 'yearly' THEN
            v_time_interval := '1 year';
        WHEN p_interval = 'quarterly' THEN
            v_time_interval := '3 months';
        WHEN p_interval = 'monthly' THEN
            v_time_interval := '1 month';
        WHEN p_interval  = 'weekly' THEN
            v_time_interval := '1 week';
        WHEN p_interval = 'daily' THEN
            v_time_interval := '1 day';
        WHEN p_interval = 'hourly' THEN
            v_time_interval := '1 hour';
        WHEN p_interval = 'half-hour' THEN
            v_time_interval := '30 mins';
        WHEN p_interval = 'quarter-hour' THEN
            v_time_interval := '15 mins';
        ELSE
            IF p_type <> 'time-custom' THEN
                RAISE EXCEPTION 'Must use a predefined time interval if not using type "time-custom". See documentation.';
            END IF;
            v_time_interval := p_interval::interval;
            IF v_time_interval < '1 second'::interval THEN
                RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
            END IF;
    END CASE;

    -- First partition is either the min premake or p_start_partition
    v_start_time := COALESCE(p_start_partition::timestamptz, CURRENT_TIMESTAMP - (v_time_interval * p_premake));

    IF v_time_interval >= '1 year' THEN
        v_base_timestamp := date_trunc('year', v_start_time);
        IF v_time_interval >= '10 years' THEN
            v_base_timestamp := date_trunc('decade', v_start_time);
            IF v_time_interval >= '100 years' THEN
                v_base_timestamp := date_trunc('century', v_start_time);
                IF v_time_interval >= '1000 years' THEN
                    v_base_timestamp := date_trunc('millennium', v_start_time);
                END IF; -- 1000
            END IF; -- 100
        END IF; -- 10
    END IF; -- 1

    v_datetime_string := 'YYYY';
    IF v_time_interval < '1 year' THEN
        IF p_interval = 'quarterly' THEN
            v_base_timestamp := date_trunc('quarter', v_start_time);
            v_datetime_string = 'YYYY"q"Q';
        ELSE
            v_base_timestamp := date_trunc('month', v_start_time); 
            v_datetime_string := v_datetime_string || '_MM';
        END IF;
        IF v_time_interval < '1 month' THEN
            IF p_interval = 'weekly' THEN
                v_base_timestamp := date_trunc('week', v_start_time);
                v_datetime_string := 'IYYY"w"IW';
            ELSE 
                v_base_timestamp := date_trunc('day', v_start_time);
                v_datetime_string := v_datetime_string || '_DD';
            END IF;
            IF v_time_interval < '1 day' THEN
                v_base_timestamp := date_trunc('hour', v_start_time);
                v_datetime_string := v_datetime_string || '_HH24MI';
                IF v_time_interval < '1 minute' THEN
                    v_base_timestamp := date_trunc('minute', v_start_time);
                    v_datetime_string := v_datetime_string || 'SS';
                END IF; -- minute
            END IF; -- day
        END IF; -- month
    END IF; -- year

    v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
    LOOP
        -- If current loop value is less than or equal to the value of the max premake, add time to array.
        IF (v_base_timestamp + (v_time_interval * v_count)) < (CURRENT_TIMESTAMP + (v_time_interval * p_premake)) THEN
            BEGIN
                v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamptz;
                v_partition_time_array := array_append(v_partition_time_array, v_partition_time);
            EXCEPTION WHEN datetime_field_overflow THEN
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                    Child partition creation after time % skipped', v_partition_time;
                v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_partition_time||' skipped');
                CONTINUE;
            END;
        ELSE
            EXIT; -- all needed partitions added to array. Exit the loop.
        END IF;
        v_count := v_count + 1;
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , epoch
        , control
        , premake
        , constraint_cols
        , datetime_string
        , use_run_maintenance
        , inherit_fk
        , jobmon 
        , upsert
        , trigger_return_null)
    VALUES (
        p_parent_table
        , p_type
        , v_time_interval
        , p_epoch
        , p_control
        , p_premake
        , p_constraint_cols
        , v_datetime_string
        , v_run_maint
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null);

    v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);

    IF v_last_partition_created = false THEN 
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- First see if this parent is a subpartition managed by pg_partman
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname, c.relname 
        INTO v_top_parent_schema, v_top_parent_table 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;
        IF v_top_parent_table IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            SELECT child_start_time INTO v_parent_partition_timestamp FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_schema||'.'||v_top_parent_table);
            IF v_base_timestamp >= v_parent_partition_timestamp THEN
                WHILE v_base_timestamp >= v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp - v_time_interval;
                END LOOP;
                v_base_timestamp := v_base_timestamp + v_time_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_base_timestamp < v_parent_partition_timestamp THEN
                WHILE v_base_timestamp < v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp + v_time_interval;
                END LOOP;
                -- Don't need to remove one since new starting time will fit in top parent interval
            END IF;
            v_partition_time_array := NULL;
            v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
            v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE EXCEPTION 'No child tables created. Unexpected edge case encountered. Please report this error to author with conditions that led to it.';
        END IF; 
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Time partitions premade: %s', p_premake));
    END IF;
END IF;

IF p_type = 'id' THEN
    v_id_interval := p_interval::bigint;
    IF v_id_interval < 10 THEN
        RAISE EXCEPTION 'Interval for serial partitioning must be greater than or equal to 10';
    END IF;

    -- Check if parent table is a subpartition of an already existing id partition set managed by pg_partman. 
    WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE n.nspname = v_higher_parent_schema::name
            AND c.relname = v_higher_parent_table::name
        ) SELECT n.nspname, c.relname
        INTO v_higher_parent_schema, v_higher_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.partition_type = 'id';

        IF v_higher_parent_table IS NOT NULL THEN
            -- v_top_parent initially set in DECLARE
            v_top_parent_schema := v_higher_parent_schema;
            v_top_parent_table := v_higher_parent_table;
        END IF;
    END LOOP;

    -- If custom start partition is set, use that.
    -- If custom start is not set and there is already data, start partitioning with the highest current value and ensure it's grabbed from highest top parent table
    IF p_start_partition IS NOT NULL THEN
        v_max := p_start_partition::bigint;
    ELSE
        v_sql := format('SELECT COALESCE(max(%I)::bigint, 0) FROM %I.%I LIMIT 1'
                    , p_control
                    , v_top_parent_schema
                    , v_top_parent_table);
        EXECUTE v_sql INTO v_max;
    END IF;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 AND 
            (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id_array = array_append(v_partition_id_array, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id_array = array_append(v_partition_id_array, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , control
        , premake
        , constraint_cols
        , use_run_maintenance
        , inherit_fk
        , jobmon
        , upsert
        , trigger_return_null) 
    VALUES (
        p_parent_table
        , p_type
        , v_id_interval
        , p_control
        , p_premake
        , p_constraint_cols
        , v_run_maint
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null);
    v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
    IF v_last_partition_created = false THEN
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- See if it's actually a subpartition of a parent id partition
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname
        WHERE p.partition_type = 'id';
        IF v_top_parent_table IS NOT NULL THEN
            -- Create the lowest possible partition that is within the boundary of the parent
             SELECT child_start_id INTO v_parent_partition_id FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_table);
            IF v_starting_partition_id >= v_parent_partition_id THEN
                WHILE v_starting_partition_id >= v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id - v_id_interval;
                END LOOP;
                v_starting_partition_id := v_starting_partition_id + v_id_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_starting_partition_id < v_parent_partition_id THEN
                WHILE v_starting_partition_id < v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id + v_id_interval;
                END LOOP;
                -- Don't need to remove one since new starting id will fit in top parent interval
            END IF;
            v_partition_id_array = NULL;
            v_partition_id_array = array_append(v_partition_id_array, v_starting_partition_id);
            v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE EXCEPTION 'No child tables created. Unexpected edge case encountered. Please report this error to author with conditions that led to it.';
        END IF;
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;
IF p_type = 'time' OR p_type = 'time-custom' THEN
    PERFORM @extschema@.create_function_time(p_parent_table, v_job_id);
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id' THEN
    PERFORM @extschema@.create_function_id(p_parent_table, v_job_id);  
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID function created');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition trigger');
END IF;
PERFORM @extschema@.create_trigger(p_parent_table);

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

v_success := true;

RETURN v_success;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE PARENT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition creation for table '||p_parent_table||' failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
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


-- Removed unneeded variable
/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_run_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */
CREATE OR REPLACE FUNCTION run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT true, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_adv_lock                      boolean;
v_check_subpart                 int;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamptz;
v_drop_count                    int := 0;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_created        boolean;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_max_id_parent                 bigint;
v_max_time_parent               timestamptz;
v_new_search_path               text := '@extschema@,pg_temp';
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamptz;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_premade_count                 int;
v_premake_id_max                bigint;
v_premake_id_min                bigint;
v_premake_timestamp_min         timestamptz;
v_premake_timestamp_max         timestamptz;
v_row                           record;
v_row_max_id                    record;
v_row_max_time                  record;
v_row_sub                       record;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_id_max                    bigint;
v_sub_id_max_suffix             bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_max_suffix      timestamptz;
v_sub_timestamp_min             timestamptz;
v_tablename                     text;
v_tables_list_sql               text;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

v_row := NULL; -- Ensure it's reset

v_tables_list_sql := 'SELECT parent_table
                , partition_type
                , partition_interval
                , control
                , premake
                , undo_in_progress
                , sub_partition_set_full
                , epoch
                , infinite_time_partitions
            FROM @extschema@.part_config
            WHERE sub_partition_set_full = false';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' AND use_run_maintenance = true';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' AND parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;

    -- Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
    -- Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
    -- This way at least provides a consistent way to check that I know will run. If anyone can get a working constraint/trigger, please help!
    -- Don't have to worry about this in the serial trigger maintenance since subpartitioning requires run_maintenance().
    SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = v_row.parent_table;
    IF v_sub_parent IS NOT NULL THEN
        SELECT count(*) INTO v_check_subpart FROM @extschema@.check_subpart_sameconfig(v_row.parent_table);
        IF v_check_subpart > 1 THEN
            RAISE EXCEPTION 'Inconsistent data in part_config_sub table. Sub-partition tables that are themselves sub-partitions cannot have differing configuration values among their siblings. 
            Run this query: "SELECT * FROM @extschema@.check_subpart_sameconfig(''%'');" This should only return a single row or nothing. 
            If multiple rows are returned, results are all children of the given parent. Update the differing values to be consistent for your desired values.', v_row.sub_parent;
        END IF;
    END IF;

    SELECT schemaname, tablename 
    INTO v_parent_schema, v_parent_tablename 
    FROM pg_catalog.pg_tables 
    WHERE schemaname = split_part(v_row.parent_table, '.', 1)::name
    AND tablename = split_part(v_row.parent_table, '.', 2)::name;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_row.partition_type = 'time' OR v_row.partition_type = 'time-custom' THEN

        SELECT child_start_time INTO v_last_partition_timestamp 
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);

        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
        FOR v_row_max_time IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            IF v_row.epoch = false THEN
                EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                                    , v_row.control
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;
            ELSE
                EXECUTE format('SELECT to_timestamp(max(%I))::text FROM %I.%I'
                                    , v_row.control
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;
            END IF;
            IF v_current_partition_timestamp IS NOT NULL THEN
                SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        IF v_row.epoch = false THEN
            EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_time_parent;
        ELSE
            EXECUTE format('SELECT to_timestamp(max(%I)) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_time_parent;
        END IF;
        IF p_debug THEN
            RAISE NOTICE 'run_maint: v_current_partition_timestamp: %, v_max_time_parent: %', v_current_partition_timestamp, v_max_time_parent;
        END IF;
        IF v_max_time_parent > v_current_partition_timestamp THEN
            SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_max_time_parent::text);
        END IF;
        IF v_current_partition_timestamp IS NULL THEN -- Partition set is completely empty
            IF v_row.infinite_time_partitions IS TRUE THEN
                -- Set it to now so new partitions continue to be created
                v_current_partition_timestamp = CURRENT_TIMESTAMP;
            ELSE
                -- Nothing to do
                CONTINUE;
            END IF;
        END IF;

        -- If this is a subpartition, determine if the last child table has been made. If so, mark it as full so future maintenance runs can skip it
        SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
        IF v_sub_timestamp_max IS NOT NULL THEN
            SELECT suffix_timestamp INTO v_sub_timestamp_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_timestamp_max::text);
            IF v_sub_timestamp_max_suffix = v_last_partition_timestamp THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        -- Check and see how many premade partitions there are.
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        v_next_partition_timestamp := v_last_partition_timestamp;
        IF p_debug THEN
            RAISE NOTICE 'run_maint before loop: current_partition_timestamp: %, v_premade_count: %, v_sub_timestamp_min: %, v_sub_timestamp_max: %'
                , v_current_partition_timestamp 
                , v_premade_count
                , v_sub_timestamp_min
                , v_sub_timestamp_max;
        END IF;
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed
        WHILE (v_premade_count < v_row.premake) LOOP
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_timestamp: %', v_row.parent_table, v_premade_count, v_next_partition_timestamp;
            END IF;
            IF v_next_partition_timestamp < v_sub_timestamp_min OR v_next_partition_timestamp > v_sub_timestamp_max THEN
                -- With subpartitioning, no need to run if the timestamp is not in the parent table's range
                EXIT;
            END IF;
            BEGIN
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.partition_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', format('Child partition creation skipped for parent table: %s', v_partition_time));
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;
            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table, ARRAY[v_next_partition_timestamp], p_analyze); 
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
            END IF;

            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        END LOOP;
    ELSIF v_row.partition_type = 'id' THEN
        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
        FOR v_row_max_id IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                            , v_row.control
                            , v_row_max_id.partition_schemaname
                            , v_row_max_id.partition_tablename) INTO v_current_partition_id;
            IF v_current_partition_id IS NOT NULL THEN
                SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_id::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_parent_tablename) INTO v_max_id_parent;
        IF v_max_id_parent > v_current_partition_id THEN
            SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_max_id_parent::text);
        END IF;
        IF v_current_partition_id IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        SELECT child_start_id INTO v_last_partition_id
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Determine if this table is a child of a subpartition parent. If so, get limits to see if run_maintenance even needs to run for it.
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'id');
        IF v_sub_id_max IS NOT NULL THEN
            SELECT suffix_id INTO v_sub_id_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_id_max::text);
            IF v_sub_id_max_suffix = v_last_partition_id THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        v_next_partition_id := v_last_partition_id;
        v_premade_count := ((v_last_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
        WHILE (v_premade_count < v_row.premake) LOOP 
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_id: %', v_row.parent_table, v_premade_count, v_next_partition_id;
            END IF;
            IF v_next_partition_id < v_sub_id_min OR v_next_partition_id > v_sub_id_max THEN
                -- With subpartitioning, no need to run if the id is not in the parent table's range
                EXIT;
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.partition_interval::bigint;
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], p_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND 
        (partition_type = 'time' OR partition_type = 'time-custom')
LOOP
    IF p_parent_table IS NULL THEN
        v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
    ELSE -- Only run retention on table given in parameter
        IF p_parent_table <> v_row.parent_table THEN
            CONTINUE;
        ELSE
            v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
        END IF;
    END IF;
    IF v_drop_count > 0 THEN
        PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
    END IF;
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND partition_type = 'id'
LOOP
    IF p_parent_table IS NULL THEN
        v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
    ELSE -- Only run retention on table given in parameter
        IF p_parent_table <> v_row.parent_table THEN
            CONTINUE;
        ELSE
            v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
        END IF;
    END IF;
    IF v_drop_count > 0 THEN
        PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
    END IF;
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Partition maintenance finished. %s partitions made. %s partitions dropped.', v_create_count, v_drop_count));
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RUN MAINTENANCE'')', v_jobmon_schema) INTO v_job_id;
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


/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_time(
        p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval interval DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true) 
    RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_current_partition_name    text;
v_epoch                     boolean;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_timestamp   timestamptz;
v_min_partition_timestamp   timestamptz;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        interval;
v_partition_suffix          text;
v_partition_timestamp       timestamptz[];
v_quarter                   text;
v_rowcount                  bigint;
v_sql                       text;
v_start_control             timestamptz;
v_time_position             int;
v_total_rows                bigint := 0;
v_type                      text;
v_year                      text;

BEGIN

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT partition_type
    , partition_interval::interval
    , control
    , datetime_string
    , epoch
INTO v_type
    , v_partition_interval
    , v_control
    , v_datetime_string
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (partition_type = 'time' OR partition_type = 'time-custom');
IF NOT FOUND THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

FOR i IN 1..p_batch_count LOOP

    IF v_epoch = false THEN
        IF p_order = 'ASC' THEN
            EXECUTE format('SELECT min(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSIF p_order = 'DESC' THEN
            EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSE
            RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
        END IF;
    ELSE
        IF p_order = 'ASC' THEN
            EXECUTE format('SELECT to_timestamp(min(%I)) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSIF p_order = 'DESC' THEN
            EXECUTE format('SELECT to_timestamp(max(%I)) FROM ONLY %I.%I', v_control, v_parent_schema, v_parent_tablename) INTO v_start_control;
        ELSE
            RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
        END IF;
    END IF;

    IF v_start_control IS NULL THEN
        EXIT;
    END IF;

    IF v_type = 'time' THEN
        CASE
            WHEN v_partition_interval = '15 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '15min'::interval * floor(date_part('minute', v_start_control) / 15.0);
            WHEN v_partition_interval = '30 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '30min'::interval * floor(date_part('minute', v_start_control) / 30.0);
            WHEN v_partition_interval = '1 hour' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control);
            WHEN v_partition_interval = '1 day' THEN
                v_min_partition_timestamp := date_trunc('day', v_start_control);
            WHEN v_partition_interval = '1 week' THEN
                v_min_partition_timestamp := date_trunc('week', v_start_control);
            WHEN v_partition_interval = '1 month' THEN
                v_min_partition_timestamp := date_trunc('month', v_start_control);
            WHEN v_partition_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_start_control);
            WHEN v_partition_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_start_control);
        END CASE;
    ELSIF v_type = 'time-custom' THEN
        SELECT child_start_time INTO v_min_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition
            , v_partition_interval::text
            , p_parent_table);
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        LOOP
            IF v_start_control >= v_min_partition_timestamp AND v_start_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                BEGIN
                    IF v_start_control > v_max_partition_timestamp THEN
                        -- Keep going forward in time, checking if child partition time interval encompasses the current v_start_control value
                        v_min_partition_timestamp := v_max_partition_timestamp;
                        v_max_partition_timestamp := v_max_partition_timestamp + v_partition_interval;

                    ELSE
                        -- Keep going backwards in time, checking if child partition time interval encompasses the current v_start_control value
                        v_max_partition_timestamp := v_min_partition_timestamp;
                        v_min_partition_timestamp := v_min_partition_timestamp - v_partition_interval;
                    END IF;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_interval;
                END;
            END IF;
        END LOOP;

    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF p_order = 'ASC' THEN
        -- Ensure batch interval given as parameter doesn't cause maximum to overflow the current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_timestamp + v_partition_interval) THEN
            v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        ELSE
            v_max_partition_timestamp := v_start_control + p_batch_interval;
        END IF;
    ELSIF p_order = 'DESC' THEN
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        -- Ensure batch interval given as parameter doesn't cause minimum to underflow current partition minimum
        IF (v_start_control - p_batch_interval) >= v_min_partition_timestamp THEN
            v_min_partition_timestamp = v_start_control - p_batch_interval;
        END IF;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

-- do some locking with timeout, if required
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                IF v_epoch = false THEN
                    v_sql := format('SELECT * FROM ONLY %I.%I WHERE %I >= %L AND %I < %L FOR UPDATE NOWAIT'
                                        , v_parent_schema
                                        , v_parent_tablename
                                        , v_control
                                        , v_min_partition_timestamp
                                        , v_control
                                        , v_max_partition_timestamp);
                ELSE
                    v_sql := format('SELECT * FROM ONLY %I.%I WHERE to_timestamp(%I) >= %L AND to_timestamp(%I) < %L FOR UPDATE NOWAIT'
                                        , v_parent_schema
                                        , v_parent_tablename
                                        , v_control
                                        , v_min_partition_timestamp
                                        , v_control
                                        , v_max_partition_timestamp);
                END IF;
                EXECUTE v_sql;
                v_lock_obtained := TRUE;
            EXCEPTION
                WHEN lock_not_available THEN
                    PERFORM pg_sleep( p_lock_wait / 5.0 );
                    CONTINUE;
            END;
            EXIT WHEN v_lock_obtained;
        END LOOP;
        IF NOT v_lock_obtained THEN
           RETURN -1;
        END IF;
    END IF;

    PERFORM @extschema@.create_partition_time(p_parent_table, v_partition_timestamp, p_analyze);
    -- This suffix generation code is in create_partition_time() as well
    v_partition_suffix := to_char(v_min_partition_timestamp, v_datetime_string);
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF v_epoch = false THEN
        v_sql := format('WITH partition_data AS (
                            DELETE FROM ONLY %I.%I WHERE %I >= %L AND %I < %L RETURNING *)
                         INSERT INTO %I.%I SELECT * FROM partition_data'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_control
                            , v_min_partition_timestamp
                            , v_control
                            , v_max_partition_timestamp
                            , v_parent_schema
                            , v_current_partition_name);
    ELSE
        v_sql := format('WITH partition_data AS (
                            DELETE FROM ONLY %I.%I WHERE to_timestamp(%I) >= %L AND to_timestamp(%I) < %L RETURNING *)
                         INSERT INTO %I.%I SELECT * FROM partition_data'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_control
                            , v_min_partition_timestamp
                            , v_control
                            , v_max_partition_timestamp
                            , v_parent_schema
                            , v_current_partition_name);
    END IF;
    EXECUTE v_sql;
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

PERFORM @extschema@.create_function_time(p_parent_table);

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

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
