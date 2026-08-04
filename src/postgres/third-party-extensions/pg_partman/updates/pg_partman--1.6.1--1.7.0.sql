-- New configuration option to allow serial partitioning to use run_maintenance() instead of creating next partition via trigger.
    -- Use "p_use_run_maintenance" argument to create_parent() to set this during partition creation.
    -- part_config table has new boolean column "use_run_maintenance"
    -- Serial/ID based partitioning defaults to FALSE. This means serial partitioning uses the parent partition trigger function to make new child partitions when the current one reaches 50% of its configured capacity (the same way it used to work). If set to TRUE, then you must schedule run_maintenance() to run often enough to keep up with your insertion rate. Otherwise rows will get inserted to the parent table.
    -- Time based partitioning defaults to TRUE and config values for using run_maintenance cannot be set to false. All time-based partitioning still requires run_maintenance() for creating new child tables.
    -- Existing partition sets have their config table values set to the defaults above.
    -- If you'd like to change an existing serial partition set to use run_maintenance instead of the trigger, update the "use_run_maintenance" column in part_config to set it to TRUE for that parent table. You must then run the "create_id_function()" function giving it a parameter of the schema qualified parent table of the set. This will remove the code in the trigger that automatically makes new child tables.
        -- Ex: SELECT partman.create_id_function('parent_schema.parent_table');
-- reapply_indexes.py can now handle too long or duplicate index names. Please see docs for how this is handled since it can change index naming patterns (Github Issue #21).
-- Fixed partition_data_id() & partition_data_time() to properly return the number of rows moved when the parent table is empty before the batch limit is reached (Github Issue #22).
-- Fixed creation of new child partition tables not working when parent tables had OIDs turned on. (Github Issue #20)
-- Fixed check_unique_constraint.py to avoid index scans and check underlying table data. Option added to try and allow index scans if desired.
-- Fixed reapply_constraints.py & reapply_indexes.py to properly run jobs in parallel.
-- Ensure an analyze is run on parent table of a set after any child table is created so that constraint exclusion works properly for all child tables.
-- Ensure an analyze is run on a child table whenever additional column constraints are automatically added. Also analyze partition set if reapply_constraints.py is run.
-- Added pgtap tests that ensure the partitioning functions are returning the proper number of rows.
-- Added pgtap tests for new features in reapply_index.py

ALTER TABLE @extschema@.part_config ADD COLUMN use_run_maintenance BOOLEAN NOT NULL DEFAULT true;
UPDATE @extschema@.part_config SET use_run_maintenance = false WHERE type = 'id-static' OR type = 'id-dynamic';

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_parent(text, text, text, text, text[], int, boolean, text, boolean, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_parent'; 

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.apply_constraints(text, text, boolean, boolean) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'apply_constraints'; 

DROP FUNCTION create_parent(text, text, text, text, text[], int, text, boolean, boolean);
DROP FUNCTION apply_constraints(text, text, boolean);

/*
 * Trigger function to enforce that time based partitioning must use run_maintenance()
 */
CREATE FUNCTION time_partition_maintenance_true_trig() RETURNS trigger 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
BEGIN

    IF TG_OP = 'INSERT' OR TG_OP = 'UPDATE' THEN
        IF NEW.type IN ('time-static', 'time-dynamic', 'time-custom') AND NEW.use_run_maintenance = FALSE THEN
            RAISE EXCEPTION 'use_run_maintenance cannot be set to FALSE for time based partitioning';
        END IF;
    END IF;
    RETURN NEW;
END
$$;

CREATE TRIGGER time_partition_maintenance_true_trig
BEFORE INSERT OR UPDATE OF type, use_run_maintenance
ON @extschema@.part_config
FOR EACH ROW EXECUTE PROCEDURE @extschema@.time_partition_maintenance_true_trig();


/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE OR REPLACE FUNCTION partition_data_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval int DEFAULT NULL, p_lock_wait numeric DEFAULT 0, p_order text DEFAULT 'ASC') RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_current_partition_name    text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_id          bigint;
v_min_partition_id          bigint;
v_part_interval             bigint;
v_partition_id              bigint[];
v_rowcount                  bigint;
v_sql                       text;
v_start_control             bigint;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
INTO v_type
    , v_part_interval
    , v_control
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

    IF p_order = 'ASC' THEN
        EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_part_interval);
        v_partition_id := ARRAY[v_min_partition_id];
        -- Check if custom batch interval overflows current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_id + v_part_interval) THEN
            v_max_partition_id := v_min_partition_id + v_part_interval;
        ELSE
            v_max_partition_id := v_start_control + p_batch_interval;
        END IF;

    ELSIF p_order = 'DESC' THEN
        EXECUTE 'SELECT max('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
        IF v_start_control IS NULL THEN
            EXIT;
        END IF;
        v_min_partition_id = v_start_control - (v_start_control % v_part_interval);
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_id := v_min_partition_id + v_part_interval;
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
                v_sql := 'SELECT * FROM ONLY ' || p_parent_table ||
                ' WHERE '||v_control||' >= '||quote_literal(v_min_partition_id)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_id)
                ||' FOR UPDATE NOWAIT';
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

    v_current_partition_name := @extschema@.create_id_partition(p_parent_table, v_partition_id);

    EXECUTE 'WITH partition_data AS (
        DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_partition_id||
            ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
        INSERT INTO '||v_current_partition_name||' SELECT * FROM partition_data';        

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP;

IF v_type = 'id-static' THEN
        PERFORM @extschema@.create_id_function(p_parent_table);
END IF;

RETURN v_total_rows;

END
$$;


/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */
CREATE OR REPLACE FUNCTION partition_data_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_lock_wait numeric DEFAULT 0, p_order text DEFAULT 'ASC') RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                   text;
v_datetime_string           text;
v_current_partition_name       text;
v_max_partition_timestamp   timestamp;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_min_partition_timestamp   timestamp;
v_part_interval             interval;
v_partition_timestamp       timestamp[];
v_rowcount                  bigint;
v_sql                       text;
v_start_control             timestamp;
v_time_position             int;
v_total_rows                bigint := 0;
v_type                      text;

BEGIN

SELECT type
    , part_interval::interval
    , control
    , datetime_string
    , last_partition
INTO v_type
    , v_part_interval
    , v_control
    , v_datetime_string
    , v_last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF p_batch_interval IS NULL OR p_batch_interval > v_part_interval THEN
    p_batch_interval := v_part_interval;
END IF;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE 'SELECT min('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
    ELSIF p_order = 'DESC' THEN
        EXECUTE 'SELECT max('||v_control||') FROM ONLY '||p_parent_table INTO v_start_control;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    IF v_start_control IS NULL THEN
        EXIT;
    END IF;

    IF v_type = 'time-static' OR v_type = 'time-dynamic' THEN
        CASE
            WHEN v_part_interval = '15 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '15min'::interval * floor(date_part('minute', v_start_control) / 15.0);
            WHEN v_part_interval = '30 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '30min'::interval * floor(date_part('minute', v_start_control) / 30.0);
            WHEN v_part_interval = '1 hour' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control);
            WHEN v_part_interval = '1 day' THEN
                v_min_partition_timestamp := date_trunc('day', v_start_control);
            WHEN v_part_interval = '1 week' THEN
                v_min_partition_timestamp := date_trunc('week', v_start_control);
            WHEN v_part_interval = '1 month' THEN
                v_min_partition_timestamp := date_trunc('month', v_start_control);
            WHEN v_part_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_start_control);
            WHEN v_part_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_start_control);
        END CASE;
    ELSIF v_type = 'time-custom' THEN
        -- Keep going backwards, checking if the time interval encompases the current v_start_control value
        v_time_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;
        v_min_partition_timestamp := to_timestamp(substring(v_last_partition from v_time_position), v_datetime_string);
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
        LOOP
            IF v_start_control >= v_min_partition_timestamp AND v_start_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                v_max_partition_timestamp := v_min_partition_timestamp;
                BEGIN
                    v_min_partition_timestamp := v_min_partition_timestamp - v_part_interval;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_interval;
                END;
            END IF;
        END LOOP;

    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF p_order = 'ASC' THEN
        IF (v_start_control + p_batch_interval) >= (v_min_partition_timestamp + v_part_interval) THEN
            v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
        ELSE
            v_max_partition_timestamp := v_start_control + p_batch_interval;
        END IF;
    ELSIF p_order = 'DESC' THEN
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
        -- Make sure minimum doesn't underflow current partition minimum
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
                v_sql := 'SELECT * FROM ONLY ' || p_parent_table ||
                ' WHERE '||v_control||' >= '||quote_literal(v_min_partition_timestamp)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)
                ||' FOR UPDATE NOWAIT';
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

    v_current_partition_name := @extschema@.create_time_partition(p_parent_table, v_partition_timestamp);

    EXECUTE 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_partition_timestamp)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_current_partition_name||' SELECT * FROM partition_data';

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

IF v_type = 'time-static' THEN
        PERFORM @extschema@.create_time_function(p_parent_table);
END IF;    

RETURN v_total_rows;

END
$$;


/*
 * Function to manage pre-creation of the next partitions in a time-based partition set.
 * Also manages dropping old partitions if the retention option is set.
 */
CREATE OR REPLACE FUNCTION run_maintenance(p_jobmon BOOLEAN DEFAULT true) RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_drop_count                    int := 0;
v_id_position                   int;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamp;
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 int;
v_quarter                       text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_step_serial_id                bigint;
v_row                           record;
v_tablename                     text;
v_time_position                 int;
v_year                          text;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

FOR v_row IN 
SELECT parent_table
    , type
    , part_interval
    , control
    , premake
    , datetime_string
    , last_partition
    , undo_in_progress
FROM @extschema@.part_config WHERE use_run_maintenance = true
LOOP

    CONTINUE WHEN v_row.undo_in_progress;
    
    -- Double check that last created partition exists
    IF v_row.last_partition IS NOT NULL THEN
        SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = v_row.last_partition;
        IF v_tablename IS NULL THEN
            RAISE EXCEPTION 'Last known partition table missing for parent table %. Unable to determine next partition in sequence.', v_row.parent_table;
        END IF;
    ELSE
        RAISE EXCEPTION 'Last known partition missing from config table for parent table %.', p_parent_table;
    END IF;

    IF v_row.type = 'time-static' OR v_row.type = 'time-dynamic' OR v_row.type = 'time-custom' THEN
        IF v_row.type = 'time-static' OR v_row.type = 'time-dynamic' THEN
            CASE
                WHEN v_row.part_interval::interval = '15 mins' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                        '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
                WHEN v_row.part_interval::interval = '30 mins' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                        '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
                WHEN v_row.part_interval::interval = '1 hour' THEN
                    v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
                 WHEN v_row.part_interval::interval = '1 day' THEN
                    v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 week' THEN
                    v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 month' THEN
                    v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '3 months' THEN
                    v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
                WHEN v_row.part_interval::interval = '1 year' THEN
                    v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
            END CASE;
        ELSIF v_row.type = 'time-custom' THEN
            SELECT child_table INTO v_current_partition FROM @extschema@.custom_time_partitions 
                WHERE parent_table = v_row.parent_table AND partition_range @> CURRENT_TIMESTAMP;
            IF v_current_partition IS NULL THEN
                RAISE EXCEPTION 'Current time partition missing from custom_time_partitions config table for table % and timestamp %',
                     CURRENT_TIMESTAMP, v_row.parent_table;
            END IF;
            v_time_position := (length(v_current_partition) - position('p_' in reverse(v_current_partition))) + 2;
            v_current_partition_timestamp := to_timestamp(substring(v_current_partition from v_time_position), v_row.datetime_string);
        END IF;

        v_time_position := (length(v_row.last_partition) - position('p_' in reverse(v_row.last_partition))) + 2;
        IF v_row.part_interval::interval <> '3 months' OR (v_row.part_interval::interval = '3 months' AND v_row.type = 'time-custom') THEN
           v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from v_time_position), v_row.datetime_string);
        ELSE
            -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
            v_year := split_part(substring(v_row.last_partition from v_time_position), 'q', 1);
            v_quarter := split_part(substring(v_row.last_partition from v_time_position), 'q', 2);
            CASE
                WHEN v_quarter = '1' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
                WHEN v_quarter = '2' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
                WHEN v_quarter = '3' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
                WHEN v_quarter = '4' THEN
                    v_last_partition_timestamp := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            END CASE;
        END IF;
        
        -- Check and see how many premade partitions there are.
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
        v_next_partition_timestamp := v_last_partition_timestamp;
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
        WHILE v_premade_count < v_row.premake LOOP
            BEGIN
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.part_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation skippd for parent table '||v_partition_time);
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;

            v_last_partition := @extschema@.create_time_partition(v_row.parent_table, ARRAY[v_next_partition_timestamp]); 
            IF v_last_partition IS NOT NULL THEN
                UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = v_row.parent_table;
            END IF;

            v_create_count := v_create_count + 1;
            IF v_row.type = 'time-static' THEN
                PERFORM @extschema@.create_time_function(v_row.parent_table);
            END IF;

            -- Manage additonal constraints if set
            PERFORM @extschema@.apply_constraints(v_row.parent_table);
            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval));
        END LOOP;
    ELSIF v_row.type = 'id-static' OR v_row.type ='id-dynamic' THEN
--        EXECUTE 'SELECT max('||v_row.control||') - ('||v_row.control||' % '||v_row.part_interval::int||') FROM '||v_row.parent_table INTO v_current_partition_id;
        EXECUTE 'SELECT '||v_row.control||' - ('||v_row.control||' % '||v_row.part_interval::int||') FROM '||v_row.parent_table||'
            WHERE '||v_row.control||' = (SELECT max('||v_row.control||') FROM '||v_row.parent_table||')' 
            INTO v_current_partition_id;
        v_id_position := (length(v_row.last_partition) - position('p_' in reverse(v_row.last_partition))) + 2;
        v_last_partition_id = substring(v_row.last_partition from v_id_position)::bigint;
        -- This catches if there's invalid data in a parent table set that's outside all child table ranges.
        IF v_last_partition_id < v_current_partition_id THEN
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_serial_id := add_step(v_job_id, 'Found inconsistent data in serial partition set.');
                PERFORM update_step(v_step_serial_id, 'CRITICAL', 'Child partition creation skipped for parent table '||v_row.parent_table||'. Current max serial id value ('||v_current_partition_id||') is greater than the id range covered by the last partition created ('||v_row.last_partition||'). Run check_parent() to find possible cause.');
            END IF;
            RAISE WARNING 'Child partition creation skipped for parent table %. Found inconsistent data in serial partition set. Current max serial id value (%) is greater than the id range covered by the last partition created (%). Run check_parent() to find possible cause.', v_row.parent_table, v_current_partition_id, v_row.last_partition;
            CONTINUE;
        END IF;
        v_next_partition_id := v_last_partition_id + v_row.part_interval::bigint;
        WHILE ((v_next_partition_id - v_current_partition_id) / v_row.part_interval::bigint) <= v_row.premake LOOP 
            v_last_partition := @extschema@.create_id_partition(v_row.parent_table, ARRAY[v_next_partition_id]);
            IF v_last_partition IS NOT NULL THEN
                UPDATE @extschema@.part_config SET last_partition = v_last_partition WHERE parent_table = v_row.parent_table;
                PERFORM @extschema@.create_id_function(v_row.parent_table);
                PERFORM @extschema@.apply_constraints(v_row.parent_table);
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.part_interval::bigint;
        END LOOP;

    END IF; -- end main IF check for time or id

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND 
        (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'id-static' OR type = 'id-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitions made. '||v_drop_count||' partitions dropped.');
    IF v_step_overflow_id IS NOT NULL OR v_step_serial_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN RUN MAINTENANCE'')' INTO v_job_id;
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


/*
 * Function to turn a table into the parent of a partition set
 */
CREATE FUNCTION create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_use_run_maintenance boolean DEFAULT NULL
    , p_start_partition text DEFAULT NULL
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_base_timestamp        timestamp;
v_count                 int := 1;
v_datetime_string       text;
v_id_interval           bigint;
v_job_id                bigint;
v_jobmon_schema         text;
v_last_partition_name   text;
v_old_search_path       text;
v_partition_time        timestamp;
v_partition_time_array  timestamp[];
v_partition_id          bigint[];
v_max                   bigint;
v_notnull               boolean;
v_run_maint             boolean;
v_start_time            timestamp;
v_starting_partition_id bigint;
v_step_id               bigint;
v_step_overflow_id      bigint;
v_tablename             text;
v_time_interval         interval;

BEGIN

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

SELECT tablename INTO v_tablename FROM pg_tables WHERE schemaname || '.' || tablename = p_parent_table;
    IF v_tablename IS NULL THEN
        RAISE EXCEPTION 'Please create given parent table first: %', p_parent_table;
    END IF;

SELECT attnotnull INTO v_notnull FROM pg_attribute WHERE attrelid = p_parent_table::regclass AND attname = p_control;
    IF v_notnull = false OR v_notnull IS NULL THEN
        RAISE EXCEPTION 'Control column (%) for parent table (%) must be NOT NULL', p_control, p_parent_table;
    END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

IF p_type = 'time-custom' AND @extschema@.check_version('9.2.0') IS FALSE THEN
    RAISE EXCEPTION 'The "time-custom" type requires a minimum PostgreSQL version of 9.2.0';
END IF;

EXECUTE 'LOCK TABLE '||p_parent_table||' IN ACCESS EXCLUSIVE MODE';

IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF p_use_run_maintenance IS NOT NULL THEN
    IF p_use_run_maintenance IS FALSE AND (p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom') THEN
        RAISE EXCEPTION 'p_run_maintenance cannot be set to false for time based partitioning';
    END IF;
    v_run_maint := p_use_run_maintenance;
ELSIF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN
    v_run_maint := TRUE;
ELSIF p_type = 'id-static' OR p_type ='id-dynamic' THEN
    v_run_maint := FALSE;
ELSE
    RAISE EXCEPTION 'use_run_maintenance value cannot be set NULL';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN SETUP PARENT: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating initial partitions on new parent table: '||p_parent_table);
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN

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
    v_start_time := COALESCE(p_start_partition::timestamp, CURRENT_TIMESTAMP - (v_time_interval * p_premake));

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
                v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamp;
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

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols, datetime_string, use_run_maintenance, jobmon) VALUES
        (p_parent_table, p_type, v_time_interval, p_control, p_premake, p_constraint_cols, v_datetime_string, v_run_maint, p_jobmon);
    v_last_partition_name := @extschema@.create_time_partition(p_parent_table, v_partition_time_array);
    -- Doing separate update because create function requires in config table last_partition to be set
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time partitions premade: '||p_premake);
    END IF;
END IF;

IF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    v_id_interval := p_interval::bigint;
    IF v_id_interval <= 1 THEN
        RAISE EXCEPTION 'Interval for serial partitioning must be greater than 1';
    END IF;
    
    -- If custom start partition is set, use that. 
    -- If custom start is not set and there is already data, start partitioning with the highest current value
    EXECUTE 'SELECT COALESCE('||quote_nullable(p_start_partition::bigint)||', max('||p_control||')::bigint, 0) FROM '||p_parent_table||' LIMIT 1' INTO v_max;
    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 
            AND (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id = array_append(v_partition_id, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id = array_append(v_partition_id, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (parent_table, type, part_interval, control, premake, constraint_cols, use_run_maintenance, jobmon) VALUES
        (p_parent_table, p_type, v_id_interval, p_control, p_premake, p_constraint_cols, v_run_maint, p_jobmon);
    v_last_partition_name := @extschema@.create_id_partition(p_parent_table, v_partition_id);
    -- Doing separate update because create function needs parent table in config table for apply_grants()
    UPDATE @extschema@.part_config SET last_partition = v_last_partition_name WHERE parent_table = p_parent_table;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'ID partitions premade: '||p_premake);
    END IF;
    
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Creating partition function');
END IF;

IF p_type = 'time-static' OR p_type = 'time-dynamic' OR p_type = 'time-custom' THEN
    PERFORM @extschema@.create_time_function(p_parent_table);
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Time function created');
    END IF;
ELSIF p_type = 'id-static' OR p_type = 'id-dynamic' THEN
    PERFORM @extschema@.create_id_function(p_parent_table);  
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
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE PARENT: '||p_parent_table||''')' INTO v_job_id;
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''Partition creation for table '||p_parent_table||' failed'')' INTO v_step_id;
            ELSIF v_step_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_step('||v_job_id||', ''EXCEPTION before first step logged'')' INTO v_step_id;
            END IF;
            EXECUTE 'SELECT '||v_jobmon_schema||'.update_step('||v_step_id||', ''CRITICAL'', ''ERROR: '||coalesce(SQLERRM,'unknown')||''')';
            EXECUTE 'SELECT '||v_jobmon_schema||'.fail_job('||v_job_id||')';
        END IF;
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE OR REPLACE FUNCTION create_id_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_count                         int;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_function_name                 text;
v_job_id                        bigint;
v_jobmon                        text;
v_jobmon_schema                 text;
v_last_partition                text;
v_max                           bigint;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_run_maint                     boolean;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT type
    , part_interval::bigint
    , control
    , premake
    , last_partition
    , use_run_maintenance
    , jobmon
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_last_partition
    , v_run_maint
    , v_jobmon
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;
v_function_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, '_part_trig_func', FALSE);

IF v_type = 'id-static' THEN
    EXECUTE 'SELECT COALESCE(max('||v_control||'), 0) FROM '||p_parent_table INTO v_max;
    v_current_partition_id = v_max - (v_max % v_part_interval);
    v_next_partition_id := v_current_partition_id + v_part_interval;
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_current_partition_id::text, TRUE);

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id  bigint;
            v_last_partition        text := '||quote_literal(v_last_partition)||';
            v_id_position           int;
            v_next_partition_id     bigint;
            v_next_partition_name   text;         
        BEGIN
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||v_current_partition_id||' AND NEW.'||v_control||' < '||v_next_partition_id|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';

        FOR i IN 1..v_premake LOOP
            v_prev_partition_id := v_current_partition_id - (v_part_interval * i);
            v_next_partition_id := v_current_partition_id + (v_part_interval * i);
            v_final_partition_id := v_next_partition_id + v_part_interval;
            v_prev_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_prev_partition_id::text, TRUE);
            v_next_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_next_partition_id::text, TRUE);
            
            -- Check that child table exist before making a rule to insert to them.
            -- Handles edge case of changing premake immediately after running create_parent(). 
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_prev_partition_name;
            IF v_count > 0 THEN
                -- Only handle previous partitions if they're starting above zero
                IF v_prev_partition_id >= 0 THEN
                    v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_prev_partition_id + v_part_interval|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); ';
                END IF;
            END IF;
            
            SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname ||'.'||tablename = v_next_partition_name;
            IF v_count > 0 THEN
                v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_next_partition_id||' AND NEW.'||v_control||' < '||v_final_partition_id|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*);'; 
            END IF;
        END LOOP;
        v_trig_func := v_trig_func ||'
            ELSE
                RETURN NEW;
            END IF;';

        IF v_run_maint IS FALSE THEN
            v_trig_func := v_trig_func ||'
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_next_partition_id := (substring(v_last_partition from v_id_position)::bigint) + '||v_part_interval||';
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||');
                    PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;';
        END IF;

        v_trig_func := v_trig_func ||'
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current id interval: '||v_current_partition_id||' to '||v_final_partition_id-1);
    END IF;

ELSIF v_type = 'id-dynamic' THEN
    -- The return inside the partition creation check is there to keep really high ID values from creating new partitions.
    v_trig_func := 'CREATE OR REPLACE FUNCTION '||v_function_name||'() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := '||quote_literal(v_last_partition)||';
            v_last_partition_id         bigint;
            v_next_partition_id         bigint;
            v_next_partition_name       text;   
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := @extschema@.check_name_length('''||v_parent_tablename||''', '''||v_parent_schema||''', v_current_partition_id::text, TRUE);
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_current_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;';

        IF v_run_maint IS FALSE THEN
            v_trig_func := v_trig_func ||'
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_id_position := (length(v_last_partition) - position(''p_'' in reverse(v_last_partition))) + 2;
                v_last_partition_id = substring(v_last_partition from v_id_position)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF NEW.'||v_control||' >= v_next_partition_id THEN
                    RETURN NEW;
                END IF;
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', ARRAY[v_next_partition_id]);
                    IF v_next_partition_name IS NOT NULL THEN
                        UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                        PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||');
                        PERFORM @extschema@.apply_constraints('||quote_literal(p_parent_table)||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;';
        END IF;

        v_trig_func := v_trig_func ||'
        END IF;
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic id table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid id partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE FUNCTION: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to create id partitions
 */
CREATE OR REPLACE FUNCTION create_id_partition (p_parent_table text, p_partition_ids bigint[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze           boolean := FALSE;
v_control           text;
v_grantees          text[];
v_hasoids           boolean;
v_id                bigint;
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_old_search_path   text;
v_parent_grant      record;
v_parent_owner      text;
v_parent_schema     text;
v_parent_tablename  text;
v_parent_tablespace text;
v_part_interval     bigint;
v_partition_name    text;
v_revoke            text[];
v_sql               text;
v_step_id           bigint;
v_tablename         text;

BEGIN

SELECT control
    , part_interval
    , jobmon
INTO v_control
    , v_part_interval
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_id IN ARRAY p_partition_ids LOOP
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_id::text, TRUE);

    -- If child table already exists, skip creation
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_part_interval)-1);
    END IF;

    v_sql := 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT relhasoids INTO v_hasoids FROM pg_class WHERE oid::regclass = p_parent_table::regclass;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE 'ALTER TABLE '||v_partition_name||' SET TABLESPACE '||v_parent_tablespace;
    END IF;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check 
        CHECK ('||v_control||'>='||quote_literal(v_id)||' AND '||v_control||'<'||quote_literal(v_id + v_part_interval)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        PERFORM close_job(v_job_id);
    END IF;

END LOOP;

IF v_analyze THEN
    EXECUTE 'ANALYZE '||p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE TABLE: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_partition_times timestamp[]) 
RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_grantees                      text[];
v_hasoids                       boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_part_interval                 interval;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_tablename                     text;
v_trunc_value                   text;
v_time                          timestamp;
v_type                          text;
v_year                          text;

BEGIN

SELECT type
    , control
    , part_interval
    , jobmon
INTO v_type
    , v_control
    , v_part_interval
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic' OR type = 'time-custom');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

SELECT tableowner, schemaname, tablename, tablespace INTO v_parent_owner, v_parent_schema, v_parent_tablename, v_parent_tablespace FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    v_partition_suffix := to_char(v_time, 'YYYY');
    IF v_part_interval < '1 year' AND v_part_interval <> '1 week' THEN 
        v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'MM');
        IF v_part_interval < '1 month' AND v_part_interval <> '1 week' THEN 
            v_partition_suffix := v_partition_suffix ||'_'|| to_char(v_time, 'DD');
            IF v_part_interval < '1 day' THEN
                v_partition_suffix := v_partition_suffix || '_' || to_char(v_time, 'HH24MI');
                IF v_part_interval < '1 minute' THEN
                    v_partition_suffix := v_partition_suffix || to_char(v_time, 'SS');
                END IF; -- end < minute IF
            END IF; -- end < day IF      
        END IF; -- end < month IF
    END IF; -- end < year IF

    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_part_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');
        CONTINUE;
    END;

    IF v_part_interval = '1 week' THEN
        v_partition_suffix := to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
    END IF;
 
    -- "Q" is ignored in to_timestamp, so handle special case
    IF v_part_interval = '3 months' AND (v_type = 'time-static' OR v_type = 'time-dynamic') THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_suffix := v_year || 'q' || v_quarter;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    v_sql := 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING INDEXES INCLUDING STORAGE INCLUDING COMMENTS)';
    SELECT relhasoids INTO v_hasoids FROM pg_class WHERE oid::regclass = p_parent_table::regclass;
    IF v_hasoids IS TRUE THEN
        v_sql := v_sql || ' WITH (OIDS)';
    END IF;
    EXECUTE v_sql;
    SELECT tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE 'ALTER TABLE '||v_partition_name||' SET TABLESPACE '||v_parent_tablespace;
    END IF;
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||v_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||v_control||'<'||quote_literal(v_partition_timestamp_end)||')';
    EXECUTE 'ALTER TABLE '||v_partition_name||' INHERIT '||p_parent_table;

    -- If custom time, set extra config options.
    IF v_type = 'time-custom' THEN
        INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
        VALUES ( p_parent_table, v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
    END IF;

    FOR v_parent_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types, grantee
        FROM information_schema.table_privileges 
        WHERE table_schema ||'.'|| table_name = p_parent_table
        GROUP BY grantee 
    LOOP
        EXECUTE 'GRANT '||array_to_string(v_parent_grant.types, ',')||' ON '||v_partition_name||' TO '||v_parent_grant.grantee;
        SELECT array_agg(r) INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE '||array_to_string(v_revoke, ',')||' ON '||v_partition_name||' FROM '||v_parent_grant.grantee||' CASCADE';
        END IF;
        v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);
    END LOOP;
    -- Revoke all privileges from roles that have none on the parent
    IF v_grantees IS NOT NULL THEN
        SELECT array_agg(r) INTO v_revoke FROM (
            SELECT DISTINCT grantee::text AS r FROM information_schema.table_privileges WHERE table_schema ||'.'|| table_name = v_partition_name
            EXCEPT
            SELECT unnest(v_grantees)) x;
        IF v_revoke IS NOT NULL THEN
            EXECUTE 'REVOKE ALL ON '||v_partition_name||' FROM '||array_to_string(v_revoke, ',');
        END IF;
    END IF;

    EXECUTE 'ALTER TABLE '||v_partition_name||' OWNER TO '||v_parent_owner;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
        IF v_step_overflow_id IS NOT NULL THEN
            PERFORM fail_job(v_job_id);
        ELSE
            PERFORM close_job(v_job_id);
        END IF;
    END IF;

END LOOP;

IF v_analyze THEN
    EXECUTE 'ANALYZE '||p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE TABLE: '||p_parent_table||''')' INTO v_job_id;
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


/*
 * Apply constraints managed by partman extension
 */
CREATE FUNCTION apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT TRUE, p_debug boolean DEFAULT FALSE) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

v_child_table                   text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_datetime_string               text;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             int; 
v_last_partition_timestamp      timestamp;
v_constraint_values             record;
v_old_search_path               text;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_interval                 text;
v_partition_suffix              text;
v_premake                       int;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN

SELECT type
    , part_interval
    , premake
    , datetime_string
    , last_partition
    , constraint_cols
    , jobmon
INTO v_type
    , v_part_interval
    , v_premake
    , v_datetime_string
    , v_last_partition
    , v_constraint_cols
    , v_jobmon
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF v_constraint_cols IS NULL THEN
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        SELECT current_setting('search_path') INTO v_old_search_path;
        EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE CONSTRAINT: '||p_parent_table);
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

-- If p_child_table is null, figure out the partition that is the one right before the premake value backwards.
IF p_child_table IS NULL THEN
    
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Automatically determining most recent child on which to apply constraints');
    END IF;

    v_suffix_position := (length(v_last_partition) - position('p_' in reverse(v_last_partition))) + 2;

    IF v_type IN ('time-static', 'time-dynamic') THEN
        v_last_partition_timestamp := to_timestamp(substring(v_last_partition from v_suffix_position), v_datetime_string);
        v_partition_suffix := to_char(v_last_partition_timestamp - (v_part_interval::interval * ((v_premake * 2)+1) ), v_datetime_string);
    ELSIF v_type IN ('id-static', 'id-dynamic') THEN
        v_last_partition_id := substring(v_last_partition from v_suffix_position)::int;
        v_partition_suffix := (v_last_partition_id - (v_part_interval::int * ((v_premake * 2)+1) ))::text; 
    END IF;
    
    v_child_table := @extschema@.check_name_length(v_parent_tablename, v_parent_schema, v_partition_suffix, TRUE);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Target child table: '||v_child_table);
    END IF;
ELSE
    v_child_table := p_child_table;
END IF;
    
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Checking if target child table exists');
END IF;

SELECT tablename INTO v_child_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_child_table;
IF v_child_tablename IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', 'Target child table ('||v_child_table||') does not exist. Skipping constraint creation.');
        PERFORM close_job(v_job_id);
        EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
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
    SELECT c.conname
    INTO v_existing_constraint_name
    FROM pg_catalog.pg_constraint c 
        JOIN pg_catalog.pg_attribute a ON c.conrelid = a.attrelid 
    WHERE conrelid = v_child_table::regclass 
        AND c.conname LIKE 'partmanconstr_%'
        AND c.contype = 'c' 
        AND a.attname = v_col
        AND ARRAY[a.attnum] <@ c.conkey 
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying new constraint on column: '||v_col);
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Partman managed constraint already exists on this table ('||v_child_table||') and column ('||v_col||'). Skipping creation.');
        END IF;
        RAISE WARNING 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_table, v_col ;
        CONTINUE;
    END IF;

    -- Ensure column name gets put on end of constraint name to help avoid naming conflicts 
    v_constraint_name := @extschema@.check_name_length('partmanconstr_'||v_child_tablename, p_suffix := '_'||v_col);

    EXECUTE 'SELECT min('||v_col||')::text AS min, max('||v_col||')::text AS max FROM '||v_child_table INTO v_constraint_values;

    IF v_constraint_values IS NOT NULL THEN
        v_sql := concat('ALTER TABLE ', v_child_table, ' ADD CONSTRAINT ', v_constraint_name
            , ' CHECK (', v_col, ' >= ', quote_literal(v_constraint_values.min), ' AND '
            , v_col, ' <= ', quote_literal(v_constraint_values.max), ')' );
        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'New constraint created: '||v_sql);
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', 'Given column ('||v_col||') contains all NULLs. No constraint created');
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    EXECUTE 'ANALYZE '||p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE 'SELECT '||v_jobmon_schema||'.add_job(''PARTMAN CREATE CONSTRAINT: '||p_parent_table||''')' INTO v_job_id;
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

DROP TABLE partman_preserve_privs_temp;
