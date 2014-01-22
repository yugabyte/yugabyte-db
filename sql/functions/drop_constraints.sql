/*
 * Drop constraints managed by pg_partman
 */
CREATE FUNCTION drop_constraints(p_parent_table text, p_child_table text, p_debug boolean DEFAULT false) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_col                           text;
v_constraint_cols               text[]; 
v_existing_constraint_name      text;
v_exists                        boolean := FALSE;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_old_search_path               text;
v_sql                           text;
v_step_id                       bigint;

BEGIN

SELECT constraint_cols 
    , jobmon
INTO v_constraint_cols 
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;

IF v_constraint_cols IS NULL THEN
    RAISE EXCEPTION 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
END IF;

IF v_jobmon THEN 
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN DROP CONSTRAINT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Entering constraint drop loop');
    PERFORM update_step(v_step_id, 'OK', 'Done');
END IF;


FOREACH v_col IN ARRAY v_constraint_cols
LOOP

    SELECT c.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint c 
        JOIN pg_catalog.pg_attribute a ON c.conrelid = a.attrelid 
    WHERE conrelid = p_child_table::regclass 
        AND c.conname LIKE 'partmanconstr_%'
        AND c.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ c.conkey 
        AND a.attisdropped = false;

    IF v_existing_constraint_name IS NOT NULL THEN
        v_exists := TRUE;
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Dropping constraint on column: '||v_col);
        END IF;
        v_sql := 'ALTER TABLE '||p_child_table||' DROP CONSTRAINT '||v_existing_constraint_name;
        IF p_debug THEN
            RAISE NOTICE 'Constraint drop query: %', v_sql;
        END IF;
        EXECUTE v_sql;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Drop constraint query: '||v_sql);
        END IF;
    END IF;

END LOOP;

IF v_jobmon_schema IS NOT NULL AND v_exists IS FALSE THEN
    v_step_id := add_step(v_job_id, 'No constraints found to drop on child table: '||p_child_table);
    PERFORM update_step(v_step_id, 'OK', 'Done');
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN DROP CONSTRAINT: '||p_parent_table||''')' INTO v_job_id;
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

