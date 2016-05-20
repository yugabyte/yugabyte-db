
/*
 * Function to undo partitioning. Copies data to parent without removing any data from children.
 * Will actually work on any parent/child table set, not just ones created by pg_partman.
 */
CREATE FUNCTION undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true, p_jobmon boolean DEFAULT true, p_lock_wait numeric DEFAULT 0) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
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
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_interval    interval;
v_rowcount              bigint;
v_step_id               bigint;
v_total                 bigint := 0;
v_trig_name             text;
v_type                  text;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition already running.';
    RETURN 0;
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
    v_job_id := add_job(format('PARTMAN UNDO PARTITIONING: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Undoing partitioning for table %s', p_parent_table));
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;

-- Stop data going into child tables and stop new id partitions from being made.
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

WHILE v_batch_loop_count < p_batch_count LOOP 
    -- Get ordered list of child table in set. Store in variable one at a time per loop until none are left.
    -- Not using show_partitions() so it can work on non-pg_partman partition sets
    WITH parent_info AS (
        SELECT c1.oid 
        FROM pg_catalog.pg_class c1 
        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
        WHERE c1.relname = v_parent_tablename::name
        AND n1.nspname = v_parent_schema::name
    )
    SELECT c.relname INTO v_child_table
    FROM pg_catalog.pg_inherits i
    JOIN pg_catalog.pg_class c ON i.inhrelid = c.oid
    JOIN parent_info p ON i.inhparent = p.oid
    ORDER BY i.inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Removing child partition: %s.%s', v_parent_schema, v_child_table));
    END IF;

    -- lockwait timeout for table drop
    v_lock_obtained := FALSE; -- reset for reuse in loop
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

    v_copy_sql := format('INSERT INTO %I.%I SELECT * FROM %I.%I'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_parent_schema
                            , v_child_table);
    EXECUTE v_copy_sql;
    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total := v_total + v_rowcount;

    EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                    , v_parent_schema
                    , v_child_table
                    , v_parent_schema
                    , v_parent_tablename);
    IF p_keep_table = false THEN
        EXECUTE format('DROP TABLE %I.%I', v_parent_schema, v_child_table);
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('Child table DROPPED. Moved %s rows to parent', v_rowcount));
        END IF;
    ELSE
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('Child table UNINHERITED, not DROPPED. Copied %s rows to parent', v_rowcount));
        END IF;
    END IF;

    SELECT partition_type INTO v_type FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    IF v_type = 'time-custom' THEN
        DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_parent_schema||'.'||v_child_table;
    END IF;

    v_batch_loop_count := v_batch_loop_count + 1;
    v_undo_count := v_undo_count + 1;
END LOOP; -- v_batch_loop_count

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
    PERFORM update_step(v_step_id, 'OK', format('Copied %s row(s) from %s child table(s) to the parent', v_total, v_undo_count));
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

