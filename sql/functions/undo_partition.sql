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

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
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


