-- Refactor of time functions in relation to epoch partitioning to improve maintainability of the code. Much thanks to Michael Rasmussen (Trekoid)! (Github Pull Request #143)
-- Some code cleanup for python scripts. Much thanks to Rodolphe Quiédeville (rodo)! (Github Pull Request #148)


/*
 * Create the trigger function for the parent table of a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_function_time(p_parent_table text, p_job_id bigint DEFAULT NULL) RETURNS void
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
v_current_partition_timestamp   timestamptz;
v_datetime_string               text;
v_epoch                         boolean;
v_final_partition_timestamp     timestamptz;
v_function_name                 text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_new_length                    int;
v_next_partition_name           text;
v_next_partition_timestamp      timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_expression          text;
v_partition_interval            interval;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamptz;
v_step_id                       bigint;
v_trig_func                     text;
v_optimize_trigger              int;
v_trigger_exception_handling    boolean;
v_trigger_return_null           boolean;
v_type                          text;
v_upsert                        text;

BEGIN

SELECT partition_type
    , partition_interval::interval
    , epoch
    , control
    , optimize_trigger
    , datetime_string
    , jobmon
    , trigger_exception_handling
    , upsert
    , trigger_return_null
INTO v_type
    , v_partition_interval
    , v_epoch
    , v_control
    , v_optimize_trigger
    , v_datetime_string
    , v_jobmon
    , v_trigger_exception_handling
    , v_upsert
    , v_trigger_return_null
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (partition_type = 'time' OR partition_type = 'time-custom');

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

v_partition_expression := case
    when v_epoch = true then format('to_timestamp(NEW.%I)', v_control)
    else format('NEW.%I', v_control)
end;

IF v_type = 'time' THEN
    v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$
            DECLARE
            v_count                 int;
            v_partition_name        text;
            v_partition_timestamp   timestamptz;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            '
    , v_parent_schema
    , v_function_name);

    CASE
        WHEN v_partition_interval = '15 mins' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s) +
                ''15min''::interval * floor(date_part(''minute'', %1$s) / 15.0);' , v_partition_expression);
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) +
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_partition_interval = '30 mins' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s) +
                ''30min''::interval * floor(date_part(''minute'', %1$s) / 30.0);' , v_partition_expression);
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) +
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_partition_interval = '1 hour' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''hour'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_partition_interval = '1 day' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''day'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_partition_interval = '1 week' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''week'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_partition_interval = '1 month' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''month'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_partition_interval = '3 months' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''quarter'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_partition_interval = '1 year' THEN
            v_trig_func := v_trig_func||format('v_partition_timestamp := date_trunc(''year'', %s);', v_partition_expression);
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;

    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, to_char(v_current_partition_timestamp, v_datetime_string), TRUE); 
    v_next_partition_timestamp := v_current_partition_timestamp + v_partition_interval::interval;

    v_trig_func := v_trig_func ||format('
            IF %s >= %L AND %1$s < %3$L THEN '
            , v_partition_expression
            , v_current_partition_timestamp
            , v_next_partition_timestamp);

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
        v_prev_partition_timestamp := v_current_partition_timestamp - (v_partition_interval::interval * i);
        v_next_partition_timestamp := v_current_partition_timestamp + (v_partition_interval::interval * i);
        v_final_partition_timestamp := v_next_partition_timestamp + (v_partition_interval::interval);
        v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, to_char(v_prev_partition_timestamp, v_datetime_string), TRUE);
        v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, to_char(v_next_partition_timestamp, v_datetime_string), TRUE);

        -- Check that child table exist before making a rule to insert to them.
        -- Handles optimize_trigger being larger than premake (to go back in time further) and edge case of changing optimize_trigger immediately after running create_parent().
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_prev_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format('
            ELSIF %s >= %L AND %1$s < %3$L THEN 
                INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_partition_expression
                , v_prev_partition_timestamp
                , v_prev_partition_timestamp + v_partition_interval::interval
                , v_parent_schema
                , v_prev_partition_name
                , v_upsert);
        END IF;
        SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_next_partition_name::name;
        IF v_count > 0 THEN
            v_trig_func := v_trig_func ||format(' 
            ELSIF %s >= %L AND %1$s < %3$L THEN 
                INSERT INTO %I.%I VALUES (NEW.*) %s;'
                , v_partition_expression
                , v_next_partition_timestamp
                , v_final_partition_timestamp
                , v_parent_schema
                , v_next_partition_name
                , v_upsert);
        END IF;

    END LOOP;

    v_trig_func := v_trig_func||format('
            ELSE
                v_partition_name := @extschema@.check_name_length(%L, to_char(v_partition_timestamp, %L), TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = %L::name AND tablename = v_partition_name::name;
                IF v_count > 0 THEN 
                    EXECUTE format(''INSERT INTO %%I.%%I VALUES($1.*) %s'', %L, v_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;'
            , v_parent_tablename
            , v_datetime_string
            , v_parent_schema
            , v_upsert
            , v_parent_schema);

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
        PERFORM update_step(v_step_id, 'OK', format('Added function for current time interval: %s to %s' 
                                                        , v_current_partition_timestamp
                                                        , v_final_partition_timestamp-'1sec'::interval));
    END IF;

ELSIF v_type = 'time-custom' THEN

    v_trig_func := format('CREATE OR REPLACE FUNCTION %I.%I() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_child_schemaname  text;
            v_child_table       text;
            v_child_tablename   text;
            v_upsert            text;
        BEGIN
            '
        , v_parent_schema
        , v_function_name);

    v_trig_func := v_trig_func || format(' 
        SELECT c.child_table, p.upsert INTO v_child_table, v_upsert
        FROM @extschema@.custom_time_partitions c
        JOIN @extschema@.part_config p ON c.parent_table = p.parent_table
        WHERE c.partition_range @> %s 
        AND c.parent_table = %L;'
        , v_partition_expression
        , v_parent_schema||'.'||v_parent_tablename);

    v_trig_func := v_trig_func || '
        SELECT schemaname, tablename INTO v_child_schemaname, v_child_tablename 
        FROM pg_catalog.pg_tables 
        WHERE schemaname = split_part(v_child_table, ''.'', 1)::name
        AND tablename = split_part(v_child_table, ''.'', 2)::name;
        IF v_child_schemaname IS NOT NULL AND v_child_tablename IS NOT NULL THEN
            EXECUTE format(''INSERT INTO %I.%I VALUES ($1.*) %s'', v_child_schemaname, v_child_tablename, v_upsert) USING NEW;
        ELSE
            RETURN NEW;
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
        PERFORM update_step(v_step_id, 'OK', format('Added function for custom time table: %s', p_parent_table));
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid time partitioning type given: %', v_type;
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
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_partition_time(p_parent_table text, p_partition_times timestamptz[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_datetime_string               text;
v_exists                        text;
v_epoch                         boolean;
v_grantees                      text[];
v_hasoids                       boolean;
v_inherit_fk                    boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_created             boolean := false;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_partition_expression          text;
v_partition_interval            interval;
v_partition_timestamp_end       timestamptz;
v_partition_timestamp_start     timestamptz;
v_quarter                       text;
v_revoke                        text;
v_row                           record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_min             timestamptz;
v_trunc_value                   text;
v_time                          timestamptz;
v_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN

SELECT partition_type
    , control
    , partition_interval
    , epoch
    , inherit_fk
    , jobmon
    , datetime_string
INTO v_type
    , v_control
    , v_partition_interval
    , v_epoch
    , v_inherit_fk
    , v_jobmon
    , v_datetime_string
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (partition_type = 'time' OR partition_type = 'time-custom');

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

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');

SELECT tableowner, schemaname, tablename, tablespace 
INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

v_partition_expression := case
    when v_epoch = true then format('to_timestamp(%I)', v_control)
    else format('%I', v_control)
end;
IF p_debug THEN
    RAISE NOTICE 'create_partition_time: v_partition_expression: %', v_partition_expression;
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    
    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_partition_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');
        CONTINUE;
    END;

    -- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_timestamp_min IS NOT NULL THEN
        IF v_time < v_sub_timestamp_min OR v_time > v_sub_timestamp_max THEN
            CONTINUE;
        END IF;
    END IF;

    -- This suffix generation code is in partition_data_time() as well
    v_partition_suffix := to_char(v_time, v_datetime_string);
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);
    SELECT tablename INTO v_exists FROM pg_catalog.pg_tables WHERE schemaname = v_parent_schema::name AND tablename = v_partition_name::name;
    IF v_exists IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Creating new partition %s.%s with interval from %s to %s'
                                                , v_parent_schema
                                                , v_partition_name
                                                , v_partition_timestamp_start
                                                , v_partition_timestamp_end-'1sec'::interval));
    END IF;

    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    v_sql := 'CREATE';
    IF v_unlogged = 'u' THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)'
                                , v_parent_schema
                                , v_partition_name
                                , v_parent_schema
                                , v_parent_tablename);
    SELECT relhasoids INTO v_hasoids 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
    END IF;
    EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%s >= %L AND %4$s < %6$L)'
        , v_parent_schema
        , v_partition_name
        , v_partition_name||'_partition_check'
        , v_partition_expression
        , v_partition_timestamp_start
        , v_partition_timestamp_end);
    IF v_epoch = true THEN
        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                        , v_parent_schema
                        , v_partition_name
                        , v_partition_name||'_partition_int_check'
                        , v_control
                        , EXTRACT('epoch' from v_partition_timestamp_start)
                        , v_control
                        , EXTRACT('epoch' from v_partition_timestamp_end) );
    END IF;

    EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I'
                    , v_parent_schema
                    , v_partition_name
                    , v_parent_schema
                    , v_parent_tablename);

    -- If custom time, set extra config options.
    IF v_type = 'time-custom' THEN
        INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
        VALUES ( p_parent_table, v_parent_schema||'.'||v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
    END IF;

    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);

    PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

    IF v_inherit_fk THEN
        PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_use_run_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
            , sub_trigger_exception_handling
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Subpartitioning %s.%s', v_parent_schema, v_partition_name));
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
                , p_epoch := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_use_run_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
            , trigger_exception_handling = v_row.sub_trigger_exception_handling
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

    END LOOP; -- end sub partitioning LOOP

    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s. Attempted intervals: %s', p_parent_table, p_partition_times));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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
CREATE OR REPLACE FUNCTION partition_data_time(
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
v_partition_expression      text;
v_partition_interval        interval;
v_partition_suffix          text;
v_partition_timestamp       timestamptz[];
v_quarter                   text;
v_rowcount                  bigint;
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

v_partition_expression := case
    when v_epoch = true then format('to_timestamp(%I)', v_control)
    else format('%I', v_control)
end;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_parent_tablename) INTO v_start_control;
    ELSIF p_order = 'DESC' THEN
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_parent_tablename) INTO v_start_control;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
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
                EXECUTE format('SELECT * FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L FOR UPDATE NOWAIT'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_partition_expression
                    , v_min_partition_timestamp
                    , v_max_partition_timestamp);
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

    EXECUTE format('WITH partition_data AS (
                        DELETE FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L RETURNING *)
                     INSERT INTO %I.%I SELECT * FROM partition_data'
                        , v_parent_schema
                        , v_parent_tablename
                        , v_partition_expression
                        , v_min_partition_timestamp
                        , v_max_partition_timestamp
                        , v_parent_schema
                        , v_current_partition_name);
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
v_partition_expression          text;
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

    v_partition_expression := case
        when v_row.epoch = true then format('to_timestamp(%I)', v_row.control)
        else format('%I', v_row.control)
    end;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: v_partition_expression: %', v_partition_expression;
    END IF;

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
            EXECUTE format('SELECT max(%s)::text FROM %I.%I'
                                , v_partition_expression
                                , v_row_max_time.partition_schemaname
                                , v_row_max_time.partition_tablename
                            ) INTO v_current_partition_timestamp;
            IF v_current_partition_timestamp IS NOT NULL THEN
                SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_parent_tablename) INTO v_max_time_parent;
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
 * Function to undo time-based partitioning created by this extension
 */
CREATE OR REPLACE FUNCTION undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true, p_lock_wait numeric DEFAULT 0) RETURNS bigint 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_min             timestamptz;
v_child_table           text;
v_control               text;
v_epoch                 boolean;
v_function_name         text;
v_inner_loop_count      int;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_expression  text;
v_partition_interval    interval;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_sub_count             int;
v_total                 bigint := 0;
v_trig_name             text;
v_type                  text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_time'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition_time already running.';
    RETURN 0;
END IF;

SELECT partition_type
    , partition_interval::interval
    , control
    , jobmon
    , epoch
INTO v_type
    , v_partition_interval
    , v_control
    , v_jobmon
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (partition_type = 'time' OR partition_type = 'time-custom');

IF v_partition_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Check if any child tables are themselves partitioned or part of an inheritance tree. Prevent undo at this level if so.
-- Need to either lock child tables at all levels or handle the proper removal of triggers on all child tables first 
--  before multi-level undo can be performed safely.
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table)
LOOP
    SELECT count(*) INTO v_sub_count
    FROM pg_catalog.pg_inherits i
    JOIN pg_catalog.pg_class c ON i.inhparent = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_row.partition_tablename::name
    AND n.nspname = v_row.partition_schemaname::name;
    IF v_sub_count > 0 THEN
        RAISE EXCEPTION 'Child table for this parent has child table(s) itself (%). Run undo partitioning on this table or remove inheritance first to ensure all data is properly moved to parent', v_row.partition_schemaname||'.'||v_row.partition_tablename;
    END IF;
END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN UNDO PARTITIONING: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Undoing partitioning for table %s', p_parent_table));
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_partition_interval;
END IF;

SELECT schemaname, tablename 
INTO v_parent_schema, v_parent_tablename 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

v_partition_expression := case
    when v_epoch = true then format('to_timestamp(%I)', v_control)
    else format('%I', v_control)
end;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables.
v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

SELECT tgname INTO v_trig_name 
FROM pg_catalog.pg_trigger t
JOIN pg_catalog.pg_class c ON t.tgrelid = c.oid
WHERE tgname = v_trig_name::name 
AND c.relname = v_parent_tablename::name;

SELECT proname INTO v_function_name FROM pg_catalog.pg_proc p JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid WHERE n.nspname = v_parent_schema::name AND proname = v_function_name::name;

IF v_trig_name IS NOT NULL THEN
    -- lockwait for trigger drop
    IF p_lock_wait > 0  THEN
        v_lock_iter := 0;
        WHILE v_lock_iter <= 5 LOOP
            v_lock_iter := v_lock_iter + 1;
            BEGIN
                EXECUTE format('LOCK TABLE ONLY %I.%I IN ACCESS EXCLUSIVE MODE NOWAIT', v_parent_schema, v_parent_tablename);
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
    EXECUTE format('DROP TRIGGER IF EXISTS %I ON %I.%I', v_trig_name, v_parent_schema, v_parent_tablename);
END IF; -- END trigger IF
v_lock_obtained := FALSE; -- reset for reuse later

IF v_function_name IS NOT NULL THEN
    EXECUTE format('DROP FUNCTION IF EXISTS %I.%I()', v_parent_schema, v_function_name);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF (v_trig_name IS NOT NULL OR v_function_name IS NOT NULL) THEN
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
    ELSE
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process.');
    END IF;
END IF;

<<outer_child_loop>>
LOOP
    -- Get ordered list of child table in set. Store in variable one at a time per loop until none are left or batch count is reached.
    -- This easily allows it to loop over same child table until empty or move onto next child table after it's dropped
    SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC') LIMIT 1;

    EXIT outer_child_loop WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Removing child partition: %s.%s', v_parent_schema, v_child_table));
    END IF;

    EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
        IF p_lock_wait > 0  THEN
            v_lock_iter := 0;
            WHILE v_lock_iter <= 5 LOOP
                v_lock_iter := v_lock_iter + 1;
                BEGIN
                    EXECUTE format('LOCK TABLE ONLY %I.%I IN ACCESS EXCLUSIVE MODE NOWAIT', v_parent_schema, v_child_table);
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

        EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                        , v_parent_schema
                        , v_child_table
                        , v_parent_schema
                        , v_parent_tablename);
        IF p_keep_table = false THEN
            EXECUTE format('DROP TABLE %I.%I', v_parent_schema, v_child_table);
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table DROPPED. Moved %s rows to parent', v_child_loop_total));
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table UNINHERITED, not DROPPED. Moved %s rows to parent', v_child_loop_total));
            END IF;
        END IF;
        IF v_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_parent_schema||'.'||v_child_table;
        END IF;
        v_undo_count := v_undo_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
        CONTINUE outer_child_loop; -- skip data moving steps below
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
                    EXECUTE format('SELECT * FROM %I.%I WHERE %I <= %L FOR UPDATE NOWAIT'
                        , v_parent_schema
                        , v_child_table
                        , v_control
                        , v_child_min + (p_batch_interval * v_inner_loop_count));
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
        EXECUTE format('WITH move_data AS (
                                DELETE FROM %I.%I WHERE %s <= %L RETURNING *)
                              INSERT INTO %I.%I SELECT * FROM move_data'
            , v_parent_schema
            , v_child_table
            , v_partition_expression
            , v_child_min + (p_batch_interval * v_inner_loop_count)
            , v_parent_schema
            , v_parent_tablename);
        GET DIAGNOSTICS v_rowcount = ROW_COUNT;
        v_total := v_total + v_rowcount;
        v_child_loop_total := v_child_loop_total + v_rowcount;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('Moved %s rows to parent.', v_child_loop_total));
        END IF;
        EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
        v_inner_loop_count := v_inner_loop_count + 1;
        v_batch_loop_count := v_batch_loop_count + 1;

        -- Check again if table is empty and go to outer loop again to drop it if so
        EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min;
        CONTINUE outer_child_loop WHEN v_child_min IS NULL;

        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
    END LOOP inner_child_loop;
END LOOP outer_child_loop;

SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC') LIMIT 1;
IF v_child_table IS NULL THEN
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Copied % row(s) to the parent. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', format('Copied %s row(s) to the parent. Removed %s partitions.', v_total, v_undo_count));
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN UNDO PARTITIONING: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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
