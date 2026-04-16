CREATE PROCEDURE @extschema@.run_analyze(p_skip_locked boolean DEFAULT false, p_quiet boolean DEFAULT false, p_parent_table text DEFAULT NULL)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock              boolean;
v_parent_schema         text;
v_parent_tablename      text;
v_row                   record;
v_sql                   text;

BEGIN

/* YB(GH#3642): advisory lock not supported
v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_analyze'));
IF v_adv_lock = false THEN
    RAISE NOTICE 'Partman analyze already running or another session has not released its advisory lock.';
    RETURN;
END IF;
*/

FOR v_row IN SELECT parent_table FROM @extschema@.part_config
LOOP

    IF p_parent_table IS NOT NULL THEN
        IF p_parent_table != v_row.parent_table THEN
            CONTINUE;
        END IF;
    END IF;

    SELECT n.nspname, c.relname
    INTO v_parent_schema, v_parent_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_row.parent_table, '.', 1)::name
    AND c.relname = split_part(v_row.parent_table, '.', 2)::name;

    v_sql := 'ANALYZE ';
    IF p_skip_locked THEN
        v_sql := v_sql || 'SKIP LOCKED ';
    END IF;
    v_sql := format('%s %I.%I', v_sql, v_parent_schema, v_parent_tablename);

    IF p_quiet = 'false' THEN
        RAISE NOTICE 'Analyzed partitioned table: %.%', v_parent_schema, v_parent_tablename;
    END IF;
    EXECUTE v_sql;
    COMMIT;

END LOOP;

/* YB(GH#3642): advisory lock not supported
PERFORM pg_advisory_unlock(hashtext('pg_partman run_analyze'));
*/
END
$$;


