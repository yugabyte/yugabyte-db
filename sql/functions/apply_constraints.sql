/*
 * Apply constraints managed by partman extension
 */
CREATE FUNCTION apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT FALSE, p_job_id bigint DEFAULT NULL, p_debug boolean DEFAULT FALSE) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_child_table                   text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_constraint_values             record;
v_control                       text;
v_datetime_string               text;
v_epoch                         boolean;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             int; 
v_last_partition_timestamp      timestamp;
v_max_id                        bigint;
v_max_timestamp                 timestamp;
v_old_search_path               text;
v_optimize_constraint           int;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_interval            text;
v_partition_suffix              text;
v_row_max                       record;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN

SELECT partition_type
    , control
    , epoch
    , partition_interval
    , optimize_constraint
    , datetime_string
    , constraint_cols
    , jobmon
INTO v_type
    , v_control
    , v_epoch
    , v_partition_interval
    , v_optimize_constraint
    , v_datetime_string
    , v_constraint_cols
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND constraint_cols IS NOT NULL;

IF v_constraint_cols IS NULL THEN
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE CONSTRAINT: %s', p_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
END IF;

-- If p_child_table is null, figure out the partition that is the one right before the optimize_constraint value backwards.
IF p_child_table IS NULL THEN
    -- Loop through child tables starting from highest to get current max value in partition set
    -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
    FOR v_row_max IN
        SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
    LOOP
        IF (v_type = 'time' OR v_type = 'time-custom') THEN
            IF v_epoch = false THEN
                EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max.partition_schemaname, v_row_max.partition_tablename) INTO v_max_timestamp;
            ELSE
                EXECUTE format('SELECT to_timestamp(max(%I)) FROM %I.%I', v_control, v_row_max.partition_schemaname, v_row_max.partition_tablename) INTO v_max_timestamp;
            END IF;
            IF v_max_timestamp IS NOT NULL THEN
                SELECT suffix_timestamp FROM @extschema@.show_partition_name(p_parent_table, v_max_timestamp::text) INTO v_last_partition_timestamp;
                v_partition_suffix := to_char(v_last_partition_timestamp - (v_partition_interval::interval * (v_optimize_constraint + 1) ), v_datetime_string);
                EXIT;
            END IF;
        ELSIF v_type = 'id' THEN
            EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max.partition_schemaname, v_row_max.partition_tablename) INTO v_max_id;
            IF v_max_id IS NOT NULL THEN
                SELECT suffix_id FROM @extschema@.show_partition_name(p_parent_table, v_max_id::text) INTO v_last_partition_id;
                v_last_partition_id := v_last_partition_id - (v_partition_interval::int * (v_optimize_constraint + 1) );
                IF v_last_partition_id < 0 THEN
                    v_last_partition_id := 0;
                END IF;
                v_partition_suffix := v_last_partition_id::text; 
                EXIT;
            END IF;
        ELSE
            RAISE EXCEPTION 'Unknown type encountered in apply_constraints()';
        END IF;
        IF p_debug THEN
            RAISE NOTICE 'apply_constraint: p_parent_table: %, partition_schemaname: %, partition_tablename: %', p_parent_table , v_row_max.partition_schemaname, v_row_max.partition_tablename;
        END IF;
    END LOOP;
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying additional constraints: Automatically determining most recent child on which to apply constraints');
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'apply_constraint: v_parent_tablename: % , v_partition_suffix: %', v_parent_tablename, v_partition_suffix;
    END IF;
    IF v_partition_suffix IS NULL THEN
        IF p_debug THEN
            RAISE NOTICE 'No values for control column found in any child table. Unable to automatically determine which child table to apply additional constraints to';
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'WARNING', 'No values for control column found in any child table. Unable to automatically determine which child table to apply additional constraints to');
            IF p_job_id IS NULL THEN
                -- If not part of a sub-job, fail this one with a warning
                PERFORM fail_job(v_job_id, 2);
            END IF;
        END IF;
        -- Return cleanly so that if maintenance calls under this condition it produces no errors that will interfere with other partition sets.
        RETURN;
    END IF;

    v_child_table := v_parent_schema ||'.'|| @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Target child table: %s.%s', v_parent_schema, v_child_table));
    END IF;
ELSE
    v_child_table := p_child_table;
END IF;
    
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Applying additional constraints: Checking if target child table exists');
END IF;

SELECT tablename INTO v_child_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
IF v_child_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', format('Target child table (%s) does not exist. Skipping constraint creation.', v_child_table));
        IF p_job_id IS NULL THEN
            PERFORM close_job(v_job_id);
        END IF;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Target child table (%) does not exist. Skipping constraint creation.', v_child_table;
    END IF;
    RETURN;
ELSE
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

FOREACH v_col IN ARRAY v_constraint_cols
LOOP
    SELECT con.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint con
    JOIN pg_class c ON c.oid = con.conrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    JOIN pg_catalog.pg_attribute a ON con.conrelid = a.attrelid 
    WHERE c.relname = v_child_tablename
        AND n.nspname = v_parent_schema
        AND con.conname LIKE 'partmanconstr_%'
        AND con.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ con.conkey 
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Applying new constraint on column: %s', v_col));
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Partman managed constraint already exists on this table (%s) and column (%s). Skipping creation.', v_child_table, v_col));
        END IF;
        IF p_debug THEN
            RAISE NOTICE 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_table, v_col ;
        END IF;
        CONTINUE;
    END IF;

    -- Ensure column name gets put on end of constraint name to help avoid naming conflicts 
    v_constraint_name := @extschema@.check_name_length('partmanconstr_'||v_child_tablename, p_suffix := '_'||v_col);

    EXECUTE format('SELECT min(%I)::text AS min, max(%I)::text AS max FROM %I.%I', v_col, v_col, v_parent_schema, v_child_tablename) INTO v_constraint_values;

    IF v_constraint_values IS NOT NULL THEN
        v_sql := format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I <= %L)'
                            , v_parent_schema
                            , v_child_tablename
                            , v_constraint_name
                            , v_col
                            , v_constraint_values.min
                            , v_col
                            , v_constraint_values.max);
        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('New constraint created: %s', v_sql));
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Given column (%s) contains all NULLs. No constraint created', v_col));
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Running analyze on partition set: %s', p_parent_table));
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Running analyze on partition set: %', p_parent_table;
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
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
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE CONSTRAINT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


