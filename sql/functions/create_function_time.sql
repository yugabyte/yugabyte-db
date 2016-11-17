/*
 * Create the trigger function for the parent table of a time-based partition set
 */
CREATE FUNCTION create_function_time(p_parent_table text, p_job_id bigint DEFAULT NULL) RETURNS void
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


