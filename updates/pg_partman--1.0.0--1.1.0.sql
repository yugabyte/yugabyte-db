-- New python scripts in extras folder to allow partition creation and undoing using smaller commit batches, as is suggested in the documentation for the partition_data_* and undo_partition_* functions. This helps avoid transaction locks when there is a large amount of data to move around. There are also options to commit more slowly and ease the load on very busy systems.
-- Changed the ordering of batch arguments in partition_data_id() & partition_data_time(). This makes their order the same as the undo functions and is a more sensical order (I think anyway).
-- Made partition functions quieter. No more notices and just returns number of rows moved.
-- Changed the undo partition functions to remove partitions in the order they were originally created. They were doing it alphabetically before, which could cause an odd order for serial based partitioning (p100 would be before p2). Creation order may not remove them in ascending order of the data at first, which would be ideal, but it makes more sense than alphabetically.
-- Bug fix: undo_partition() could return 0 prematurely if some of the partitions were empty. Will now automatically uninherit/drop any empty partitions and continue on if there are still child tables, not counting them against p_batch_count if given.


DROP FUNCTION @extschema@.partition_data_id(text, int, int);
DROP FUNCTION @extschema@.partition_data_time(text, interval, int);

/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval int DEFAULT NULL) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_last_partition_name       text;
v_max_partition_id          bigint;
v_min_control               bigint;
v_min_partition_id          bigint;
v_part_interval             bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;

BEGIN

SELECT part_interval::bigint, control
INTO v_part_interval, v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_part_interval THEN
    p_batch_interval := v_part_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
    IF v_min_control IS NULL THEN
        RETURN 0;
    END IF;

    v_min_partition_id = v_min_control - (v_min_control % v_part_interval);

    v_partition_id := ARRAY[v_min_partition_id];
--    RAISE NOTICE 'v_partition_id: %',v_partition_id;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_id + v_part_interval) THEN
        v_max_partition_id := v_min_partition_id + v_part_interval;
    ELSE
        v_max_partition_id := v_min_control + p_batch_interval;
    END IF;
--    RAISE NOTICE 'v_max_partition_id: %',v_max_partition_id;

    v_sql := 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||v_part_interval||','||quote_literal(v_partition_id)||')';
--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
        DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_control||
            ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
        INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';        

--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql;

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

RETURN v_total_rows;

END
$$;


/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_last_partition_name       text;
v_max_partition_timestamp   timestamp;
v_min_control               timestamp;
v_min_partition_timestamp   timestamp;
v_part_interval             interval;
v_partition_timestamp       timestamp[];
v_rowcount                  bigint;
v_sql                       text;
v_total_rows                bigint := 0;

BEGIN

SELECT part_interval::interval, control, datetime_string
INTO v_part_interval, v_control, v_datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic');
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_part_interval THEN
    p_batch_interval := v_part_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_min_control;
    IF v_min_control IS NULL THEN
        RETURN 0;
    END IF;

    CASE
        WHEN v_part_interval = '15 mins' THEN
            v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
                '15min'::interval * floor(date_part('minute', v_min_control) / 15.0);
        WHEN v_part_interval = '30 mins' THEN
            v_min_partition_timestamp := date_trunc('hour', v_min_control) + 
                '30min'::interval * floor(date_part('minute', v_min_control) / 30.0);
        WHEN v_part_interval = '1 hour' THEN
            v_min_partition_timestamp := date_trunc('hour', v_min_control);
        WHEN v_part_interval = '1 day' THEN
            v_min_partition_timestamp := date_trunc('day', v_min_control);
        WHEN v_part_interval = '1 week' THEN
            v_min_partition_timestamp := date_trunc('week', v_min_control);
        WHEN v_part_interval = '1 month' THEN
            v_min_partition_timestamp := date_trunc('month', v_min_control);
        WHEN v_part_interval = '3 months' THEN
            v_min_partition_timestamp := date_trunc('quarter', v_min_control);
        WHEN v_part_interval = '1 year' THEN
            v_min_partition_timestamp := date_trunc('year', v_min_control);
    END CASE;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
--    RAISE NOTICE 'v_partition_timestamp: %',v_partition_timestamp;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_timestamp + v_part_interval) THEN
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
    ELSE
        v_max_partition_timestamp := v_min_control + p_batch_interval;
    END IF;
--    RAISE NOTICE 'v_max_partition_timestamp: %',v_max_partition_timestamp;

    v_sql := 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||quote_literal(v_part_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_timestamp)||')';
--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';
--    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql;

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

RETURN v_total_rows;

END
$$;


/*
 * Function to undo partitioning. 
 * Will actually work on any parent/child table set, not just ones created by pg_partman.
 */
CREATE OR REPLACE FUNCTION undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      bigint := 0;
v_child_count           bigint;
v_child_table           text;
v_copy_sql              text;
v_job_id                bigint;
v_jobmon_schema         text;
v_old_search_path       text;
v_part_interval         interval;
v_rowcount              bigint;
v_step_id               bigint;
v_tablename             text;
v_total                 bigint := 0;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition already running.';
    RETURN 0;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
v_tablename := substring(p_parent_table from position('.' in p_parent_table)+1);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_tablename||'_part_trig ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||p_parent_table||'_part_trig_func()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT inhrelid::regclass INTO v_child_table 
    FROM pg_catalog.pg_inherits 
    WHERE inhparent::regclass = p_parent_table::regclass 
    ORDER BY inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    EXECUTE 'SELECT count(*) FROM '||v_child_table INTO v_child_count;
    IF v_child_count = 0 THEN
        -- No rows left in this child table. Remove from partition set.
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

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to undo id-based partitioning created by this extension
 */
CREATE OR REPLACE FUNCTION undo_partition_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval bigint DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_min             bigint;
v_child_table           text;
v_control               text;
v_inner_loop_count      int;
v_job_id                bigint;
v_jobmon_schema         text;
v_move_sql              text;
v_old_search_path       text;
v_part_interval         bigint;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_tablename             text;
v_total                 bigint := 0;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_id_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_id_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::bigint
    , control
INTO v_part_interval
    , v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'id-static' OR type = 'id-dynamic');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
v_tablename := substring(p_parent_table from position('.' in p_parent_table)+1);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_tablename||'_part_trig ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||p_parent_table||'_part_trig_func()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT inhrelid::regclass INTO v_child_table 
    FROM pg_catalog.pg_inherits 
    WHERE inhparent::regclass = p_parent_table::regclass 
    ORDER BY inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.
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

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_id_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Function to undo time-based partitioning created by this extension
 */
CREATE OR REPLACE FUNCTION undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
v_batch_loop_count      int := 0;
v_child_min             timestamptz;
v_child_loop_total      bigint := 0;
v_child_table           text;
v_control               text;
v_inner_loop_count      int;
v_job_id                bigint;
v_jobmon_schema         text;
v_move_sql              text;
v_old_search_path       text;
v_part_interval         interval;
v_row                   record;
v_rowcount              bigint;
v_step_id               bigint;
v_tablename             text;
v_total                 bigint := 0;
v_undo_count            int := 0;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman undo_time_partition'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_time_partition already running.';
    RETURN 0;
END IF;

SELECT part_interval::interval
    , control
INTO v_part_interval
    , v_control
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table 
AND (type = 'time-static' OR type = 'time-dynamic');

IF v_part_interval IS NULL THEN
    RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Undoing partitioning for table '||p_parent_table);
END IF;

IF p_batch_interval IS NULL THEN
    p_batch_interval := v_part_interval;
END IF;

-- Stops new time partitons from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
v_tablename := substring(p_parent_table from position('.' in p_parent_table)+1);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_tablename||'_part_trig ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||p_parent_table||'_part_trig_func()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

<<outer_child_loop>>
WHILE v_batch_loop_count < p_batch_count LOOP 
    SELECT inhrelid::regclass INTO v_child_table 
    FROM pg_catalog.pg_inherits 
    WHERE inhparent::regclass = p_parent_table::regclass 
    ORDER BY inhrelid ASC;

    EXIT WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing child partition: '||v_child_table);
    END IF;

    EXECUTE 'SELECT min('||v_control||') FROM '||v_child_table INTO v_child_min;
    IF v_child_min IS NULL THEN
        -- No rows left in this child table. Remove from partition set.
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

PERFORM pg_advisory_unlock(hashtext('pg_partman undo_time_partition'));

RETURN v_total;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN UNDO PARTITIONING: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition function maintenance for table '||p_parent_table||' failed');
            ELSIF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;
