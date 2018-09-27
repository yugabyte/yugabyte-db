CREATE FUNCTION @extschema@.apply_foreign_keys(p_parent_table text, p_child_table text, p_job_id bigint DEFAULT NULL, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_count             int := 0;
v_job_id            bigint;
v_jobmon            text;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_schema     text;
v_parent_tablename  text;
v_ref_schema        text;
v_ref_table         text;
v_relkind           char;
v_row               record;
v_schemaname        text;
v_sql               text;
v_step_id           bigint;
v_tablename         text;

BEGIN
/*
 * Apply foreign keys that exist on the given parent to the given child table
 */

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', '@extschema@,'||v_jobmon_schema, 'false');
    END IF;
END IF;

SELECT n.nspname, c.relname, c.relkind INTO v_parent_schema, v_parent_tablename, v_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_relkind = 'p' THEN
    RAISE EXCEPTION 'This function cannot run on natively partitioned tables';
ELSIF v_relkind IS NULL THEN
    RAISE EXCEPTION 'Unable to find given table in system catalogs: %.%', v_parent_schema, v_parent_tablename;
END IF;

SELECT n.nspname, c.relname INTO v_schemaname, v_tablename 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_child_table, '.', 1)::name
AND c.relname = split_part(p_child_table, '.', 2)::name;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN APPLYING FOREIGN KEYS: %s', p_parent_table));
    ELSE -- Don't create a new job, add steps into given job
        v_job_id := p_job_id;
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, format('Applying foreign keys to %s if they exist on parent', p_child_table));
END IF;

IF v_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'CRITICAL', format('Target child table (%s) does not exist.', p_child_table));
        PERFORM fail_job(v_job_id);
        EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    END IF;
    RAISE EXCEPTION 'Target child table (%) does not exist.', p_child_table;
    RETURN;
END IF;

FOR v_row IN
    SELECT pg_get_constraintdef(con.oid) AS constraint_def 
    FROM pg_catalog.pg_constraint con
    JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename
    AND n.nspname = v_parent_schema
    AND contype = 'f'
LOOP
    v_sql := format('ALTER TABLE %I.%I ADD %s'
                    , v_schemaname
                    , v_tablename
                    , v_row.constraint_def);

    IF p_debug THEN
        RAISE NOTICE 'Constraint creation query: %', v_sql;
    END IF;

    EXECUTE v_sql;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'FK applied');
    END IF;
    v_count := v_count + 1;

END LOOP;

IF v_count = 0 AND v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'No FKs found on parent');
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
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE APPLYING FOREIGN KEYS: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


