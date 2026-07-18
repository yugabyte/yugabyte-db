-- New functions to undo partitioning. These all either move or copy data from the child tables and put it into the parent. All have an option to allow you either uninherit the child tables (default) or drop them when all their data has been put into the parent.
    --  undo_partition_time() & undo_partition_id are functions that move the data from the child partitions to the parent tables. Data is deleted from the child table and inserted to the parent. These functions allow smaller interval batches to be given as a parameter and are better able to handle larger partitioning sets. 
    -- undo_partition() can work on an any parent/child table set in PostgreSQL, not just partition sets created by pg_partman. Just pass it the name of the parent table. This method only copies the data out of the child tables instead of deleting it, allowing you to keep all the partitioned data if desired. Because of this it can only process an entire partition at a time and cannot handle batches smaller than the partition interval.
-- Changed create_prev_id_partition() to partition_data_id() & create_prev_time_partition() to partition_data_time(). This clarifies what these actually do since they don't always create a partition nor is it always necessarily "previous" data.
-- Changed how the above functions work to move data from parent into partitions. You can now feed them a smaller interval value for the rows that you'd like moved instead of it always moving exactly one entire partition of data. This allows smaller batch sizes when you've got a lot of data even in just one partition. That interval is now the second parameter. A third parameter can tell it how many of those interval batches you'd like to move in a single run of the function. Both of these parameters are optional. If not given, the interval defaults to the partition interval and the batch count is one (so it works exactly like it used to with no parameters but the parent table given). 
-- Partition premake system is now able to catch up if it falls behind for some reason. Also makes it so that if the premake value is increased, within the next few runs it will have that many partitions premade automatically.
-- Bug fix: create_time_partition() & create_time_function() now handle the "timestamp with time zone" data type much better. Was getting some mismatches in the trigger rules and table constraints when timestamptz was in use on server not running in UTC/GMT time. Would cause constraint violations during data insert at certain time boundaries. If you ran into this issue, there are two ways to fix it: 1) Manually recreate the constraints for the most recent partitions and any future partitions already created. You may have to move some data around as well. 2) Use the new undo functions to move all the data back into the parent table and then repartition again using the partition_data_* functions. This will fix the issue for all partitions.
-- Bug fix: Determining how many partitions to premake in run_maintenance() is now more accurate. Previous date math would occasionally premake 1 extra partition depending on the time differences. This can still occur with weekly partitioning due to differing month lengths (especially February) and daylight savings. Doesn't hurt anything and will self-correct.
-- Much more complete pgTAP test suite.

DROP FUNCTION create_prev_id_partition(text, int);
DROP FUNCTION create_prev_time_partition(text, int);
ALTER TABLE @extschema@.part_config ADD COLUMN undo_in_progress boolean NOT NULL DEFAULT false;

/*
 * Populate the child table(s) of an id-based partition set with old data from the original parent
 */
CREATE FUNCTION partition_data_id(p_parent_table text, p_batch_interval int DEFAULT NULL, p_batch_count int DEFAULT 1) RETURNS bigint
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
    RAISE NOTICE 'v_partition_id: %',v_partition_id;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_id + v_part_interval) THEN
        v_max_partition_id := v_min_partition_id + v_part_interval;
    ELSE
        v_max_partition_id := v_min_control + p_batch_interval;
    END IF;
    RAISE NOTICE 'v_max_partition_id: %',v_max_partition_id;

    v_sql := 'SELECT @extschema@.create_id_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||v_part_interval||','||quote_literal(v_partition_id)||')';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
        DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||v_min_control||
            ' AND '||v_control||' < '||v_max_partition_id||' RETURNING *)
        INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';        

    RAISE NOTICE 'v_sql: %', v_sql;
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
CREATE FUNCTION partition_data_time(p_parent_table text, p_batch_interval interval DEFAULT NULL, p_batch_count int DEFAULT 1) RETURNS bigint
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
    RAISE NOTICE 'v_partition_timestamp: %',v_partition_timestamp;
    IF (v_min_control + p_batch_interval) >= (v_min_partition_timestamp + v_part_interval) THEN
        v_max_partition_timestamp := v_min_partition_timestamp + v_part_interval;
    ELSE
        v_max_partition_timestamp := v_min_control + p_batch_interval;
    END IF;
    RAISE NOTICE 'v_max_partition_timestamp: %',v_max_partition_timestamp;

    v_sql := 'SELECT @extschema@.create_time_partition('||quote_literal(p_parent_table)||','||quote_literal(v_control)||','
    ||quote_literal(v_part_interval)||','||quote_literal(v_datetime_string)||','||quote_literal(v_partition_timestamp)||')';
    RAISE NOTICE 'v_sql: %', v_sql;
    EXECUTE v_sql INTO v_last_partition_name;

    v_sql := 'WITH partition_data AS (
            DELETE FROM ONLY '||p_parent_table||' WHERE '||v_control||' >= '||quote_literal(v_min_control)||
                ' AND '||v_control||' < '||quote_literal(v_max_partition_timestamp)||' RETURNING *)
            INSERT INTO '||v_last_partition_name||' SELECT * FROM partition_data';
    RAISE NOTICE 'v_sql: %', v_sql;
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
 * Function to manage pre-creation of the next partitions in a time-based partition set.
 * Also manages dropping old partitions if the retention option is set.
 */
CREATE OR REPLACE FUNCTION run_maintenance() RETURNS void 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock                      boolean;
v_create_count                  int := 0;
v_current_partition_timestamp   timestamp;
v_datetime_string               text;
v_drop_count                    int := 0;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_timestamp      timestamp;
v_old_search_path               text;
v_premade_count                 real;
v_quarter                       text;
v_step_id                       bigint;
v_row                           record;
v_year                          text;

BEGIN

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
END IF;

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

FOR v_row IN 
SELECT parent_table
    , type
    , part_interval::interval
    , control
    , premake
    , datetime_string
    , last_partition
    , undo_in_progress
FROM @extschema@.part_config WHERE type = 'time-static' OR type = 'time-dynamic'
LOOP

    CONTINUE WHEN v_row.undo_in_progress;
    
    CASE
        WHEN v_row.part_interval = '15 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_row.part_interval = '30 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_row.part_interval = '1 hour' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_row.part_interval = '1 day' THEN
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 week' THEN
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 month' THEN
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '3 months' THEN
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_row.part_interval = '1 year' THEN
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;

    IF v_row.part_interval != '3 months' THEN
        v_last_partition_timestamp := to_timestamp(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), v_row.datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), 'q', 1);
        v_quarter := split_part(substring(v_row.last_partition from char_length(v_row.parent_table||'_p')+1), 'q', 2);
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

    -- Check and see how many premade partitions there are. If it's less than premake in config table, make another
    v_premade_count = EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval);

    -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
    WHILE v_premade_count < v_row.premake LOOP
        EXECUTE 'SELECT @extschema@.create_next_time_partition('||quote_literal(v_row.parent_table)||')';
        v_create_count := v_create_count + 1;
        IF v_row.type = 'time-static' THEN
            EXECUTE 'SELECT @extschema@.create_time_function('||quote_literal(v_row.parent_table)||')';
        END IF;
        v_last_partition_timestamp := v_last_partition_timestamp + v_row.part_interval;
        v_premade_count = EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.part_interval::interval);
    END LOOP;

END LOOP; -- end of creation loop

-- Manage dropping old partitions if retention option is set
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'time-static' OR type = 'time-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_time_partition(v_row.parent_table);   
END LOOP; 
FOR v_row IN 
    SELECT parent_table FROM @extschema@.part_config WHERE retention IS NOT NULL AND undo_in_progress = false AND (type = 'id-static' OR type = 'id-dynamic')
LOOP
    v_drop_count := v_drop_count + @extschema@.drop_id_partition(v_row.parent_table);
END LOOP; 

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Partition maintenance finished. '||v_create_count||' partitions made. '||v_drop_count||' partitions dropped.');
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));

EXCEPTION
    WHEN QUERY_CANCELED THEN
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN RUN MAINTENANCE');
                v_step_id := add_step(v_job_id, 'EXCEPTION before job logging started');
            END IF;
            IF v_step_id IS NULL THEN
                v_step_id := add_step(v_job_id, 'EXCEPTION before first step logged');
            END IF;
            PERFORM update_step(v_step_id, 'CRITICAL', 'ERROR: '||coalesce(SQLERRM,'unknown'));
            PERFORM fail_job(v_job_id);
            EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
        END IF;
        PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
        RAISE EXCEPTION '%', SQLERRM;
END
$$;


/*
 * Create the trigger function for the parent table of an id-based partition set
 */
CREATE OR REPLACE FUNCTION create_id_function(p_parent_table text, p_current_id bigint) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_current_partition_name        text;
v_current_partition_id          bigint;
v_datetime_string               text;
v_final_partition_id            bigint;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition                text;
v_next_partition_id             bigint;
v_next_partition_name           text;
v_old_search_path               text;
v_part_interval                 bigint;
v_premake                       int;
v_prev_partition_id             bigint;
v_prev_partition_name           text;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT type
    , part_interval::bigint
    , control
    , premake
    , last_partition
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_last_partition
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'id-static' OR type = 'id-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_type = 'id-static' THEN
    v_current_partition_id := p_current_id - (p_current_id % v_part_interval);
    v_next_partition_id := v_current_partition_id + v_part_interval;
    v_current_partition_name := p_parent_table || '_p' || v_current_partition_id::text;

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_current_partition_id  bigint;
            v_last_partition        text := '||quote_literal(v_last_partition)||';
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
            v_prev_partition_name := p_parent_table || '_p' || v_prev_partition_id::text;
            v_next_partition_name := p_parent_table || '_p' || v_next_partition_id::text;
            -- Only make previous partitions if they're starting above zero
            IF v_prev_partition_id >= 0 THEN
                v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_prev_partition_id||' AND NEW.'||v_control||' < '||v_prev_partition_id + v_part_interval|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); ';
            END IF;
            v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||v_next_partition_id||' AND NEW.'||v_control||' < '||v_final_partition_id|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*); ';
        END LOOP;

        v_trig_func := v_trig_func ||'
            ELSE
                RETURN NEW;
            END IF;
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_next_partition_id := (substring(v_last_partition from char_length('||quote_literal(p_parent_table||'_p')||')+1)::bigint) + '||v_part_interval||';
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||v_part_interval||', ARRAY[v_next_partition_id]);
                    UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                    PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current id interval: '||v_current_partition_id||' to '||v_final_partition_id-1);
    END IF;

ELSIF v_type = 'id-dynamic' THEN
    -- The return inside the partition creation check is there to keep really high ID values from creating new partitions.
    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_last_partition            text := '||quote_literal(v_last_partition)||';
            v_last_partition_id         bigint;
            v_next_partition_id         bigint;
            v_next_partition_name       text;   
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            v_current_partition_id := NEW.'||v_control||' - (NEW.'||v_control||' % '||v_part_interval||');
            v_current_partition_name := '''||p_parent_table||'_p''||v_current_partition_id;
            IF (NEW.'||v_control||' % '||v_part_interval||') > ('||v_part_interval||' / 2) THEN
                v_last_partition_id = substring(v_last_partition from char_length('||quote_literal(p_parent_table||'_p')||')+1)::bigint;
                v_next_partition_id := v_last_partition_id + '||v_part_interval||';
                IF NEW.'||v_control||' >= v_next_partition_id THEN
                    RETURN NEW;
                END IF;
                WHILE ((v_next_partition_id - v_current_partition_id) / '||v_part_interval||') <= '||v_premake||' LOOP 
                    v_next_partition_name := @extschema@.create_id_partition('||quote_literal(p_parent_table)||', '||quote_literal(v_control)||','
                        ||quote_literal(v_part_interval)||', ARRAY[v_next_partition_id]);
                    IF v_next_partition_name IS NOT NULL THEN
                        UPDATE @extschema@.part_config SET last_partition = v_next_partition_name WHERE parent_table = '||quote_literal(p_parent_table)||';
                        PERFORM @extschema@.create_id_function('||quote_literal(p_parent_table)||', NEW.'||v_control||');
                    END IF;
                    v_next_partition_id := v_next_partition_id + '||v_part_interval||';
                END LOOP;
            END IF;
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname ||''.''|| tablename = v_current_partition_name;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_current_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;
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
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
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
 * Function to create a child table in a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_partition (p_parent_table text, p_control text, p_interval interval, p_datetime_string text, p_partition_times timestamp[]) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_grantees                      text[];
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_parent_grant                  record;
v_parent_owner                  text;
v_partition_name                text;
v_partition_timestamp_end       timestamp;
v_partition_timestamp_start     timestamp;
v_quarter                       text;
v_revoke                        text[];
v_step_id                       bigint;
v_tablename                     text;
v_trunc_value                   text;
v_time                          timestamp;
v_year                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

SELECT tableowner INTO v_parent_owner FROM pg_tables WHERE schemaname ||'.'|| tablename = p_parent_table;

FOREACH v_time IN ARRAY p_partition_times LOOP    

    v_partition_name := p_parent_table || '_p';

    IF p_interval = '1 year' OR p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'YYYY');
        v_trunc_value := 'year';

        IF p_interval = '1 month' OR p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
            v_partition_name := v_partition_name || '_' || to_char(v_time, 'MM');
            v_trunc_value := 'month';

            IF p_interval = '1 day' OR p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                v_partition_name := v_partition_name || '_' || to_char(v_time, 'DD');
                    v_trunc_value := 'day';

                IF p_interval = '1 hour' OR p_interval = '30 mins' OR p_interval = '15 mins' THEN
                    v_partition_name := v_partition_name || '_' || to_char(v_time, 'HH24');
                    IF p_interval <> '30 mins' AND p_interval <> '15 mins' THEN
                        v_partition_name := v_partition_name || '00';
                        v_trunc_value := 'hour';
                    ELSIF p_interval = '15 mins' THEN
                        IF date_part('minute', v_time) < 15 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSIF date_part('minute', v_time) >= 15 AND date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '15';
                        ELSIF date_part('minute', v_time) >= 30 AND date_part('minute', v_time) < 45 THEN
                            v_partition_name := v_partition_name || '30';
                        ELSE
                            v_partition_name := v_partition_name || '45';
                        END IF;
                        v_trunc_value := 'minute';
                    ELSIF p_interval = '30 mins' THEN
                        IF date_part('minute', v_time) < 30 THEN
                            v_partition_name := v_partition_name || '00';
                        ELSE
                            v_partition_name := v_partition_name || '30';
                        END IF;
                        v_trunc_value := 'minute';
                    END IF;
                END IF; -- end hour IF      
            END IF; -- end day IF
        END IF; -- end month IF
    ELSIF p_interval = '1 week' THEN
        v_partition_name := v_partition_name || to_char(v_time, 'IYYY') || 'w' || to_char(v_time, 'IW');
        v_trunc_value := 'week';
    END IF; -- end year/week IF

    -- pull out datetime portion of last partition's tablename if it matched one of the above partitioning intervals
    IF v_trunc_value IS NOT NULL THEN
        v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string));
        v_partition_timestamp_end := date_trunc(v_trunc_value, to_timestamp(substring(v_partition_name from char_length(p_parent_table||'_p')+1), p_datetime_string) + p_interval);
    END IF;

    -- "Q" is ignored in to_timestamp, so handle special case
    IF p_interval = '3 months' THEN
        v_year := to_char(v_time, 'YYYY');
        v_quarter := to_char(v_time, 'Q');
        v_partition_name := v_partition_name || v_year || 'q' || v_quarter;
        v_trunc_value := 'quarter';
        CASE 
            WHEN v_quarter = '1' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-01-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '2' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-04-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '3' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-07-01', 'YYYY-MM-DD'));
            WHEN v_quarter = '4' THEN
                v_partition_timestamp_start := date_trunc(v_trunc_value, to_timestamp(v_year || '-10-01', 'YYYY-MM-DD'));
        END CASE;
        v_partition_timestamp_end := date_trunc(v_trunc_value, (v_partition_timestamp_start + p_interval));
    END IF;

    SELECT schemaname ||'.'|| tablename INTO v_tablename FROM pg_catalog.pg_tables WHERE schemaname ||'.'|| tablename = v_partition_name;
    IF v_tablename IS NOT NULL THEN
        CONTINUE;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_partition_timestamp_start||' to '||(v_partition_timestamp_end-'1sec'::interval));
    END IF;

    IF position('.' in p_parent_table) > 0 THEN 
        v_tablename := substring(v_partition_name from position('.' in v_partition_name)+1);
    END IF;

    EXECUTE 'CREATE TABLE '||v_partition_name||' (LIKE '||p_parent_table||' INCLUDING DEFAULTS INCLUDING INDEXES)';
    EXECUTE 'ALTER TABLE '||v_partition_name||' ADD CONSTRAINT '||v_tablename||'_partition_check
        CHECK ('||p_control||'>='||quote_literal(v_partition_timestamp_start)||' AND '||p_control||'<'||quote_literal(v_partition_timestamp_end)||')';
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

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

RETURN v_partition_name;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE TABLE: '||p_parent_table);
                v_step_id := add_step(v_job_id, 'Partition maintenance for table '||p_parent_table||' failed');
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
 * Create the trigger function for the parent table of a time-based partition set
 */
CREATE OR REPLACE FUNCTION create_time_function(p_parent_table text) RETURNS void
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_control                       text;
v_current_partition_name        text;
v_current_partition_timestamp   timestamptz;
v_datetime_string               text;
v_final_partition_timestamp     timestamptz;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_old_search_path               text;
v_next_partition_name           text;
v_next_partition_timestamp      timestamptz;
v_part_interval                 interval;
v_premake                       int;
v_prev_partition_name           text;
v_prev_partition_timestamp      timestamptz;
v_step_id                       bigint;
v_trig_func                     text;
v_type                          text;

BEGIN

SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
IF v_jobmon_schema IS NOT NULL THEN
    SELECT current_setting('search_path') INTO v_old_search_path;
    EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
    v_step_id := add_step(v_job_id, 'Creating partition function for table '||p_parent_table);
END IF;

SELECT type
    , part_interval::interval
    , control
    , premake
    , datetime_string
INTO v_type
    , v_part_interval
    , v_control
    , v_premake
    , v_datetime_string
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table
AND (type = 'time-static' OR type = 'time-dynamic');

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

IF v_type = 'time-static' THEN

    CASE
        WHEN v_part_interval = '15 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '15min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 15.0);
        WHEN v_part_interval = '30 mins' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP) + 
                '30min'::interval * floor(date_part('minute', CURRENT_TIMESTAMP) / 30.0);
        WHEN v_part_interval = '1 hour' THEN
            v_current_partition_timestamp := date_trunc('hour', CURRENT_TIMESTAMP);
         WHEN v_part_interval = '1 day' THEN
            v_current_partition_timestamp := date_trunc('day', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 week' THEN
            v_current_partition_timestamp := date_trunc('week', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 month' THEN
            v_current_partition_timestamp := date_trunc('month', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '3 months' THEN
            v_current_partition_timestamp := date_trunc('quarter', CURRENT_TIMESTAMP);
        WHEN v_part_interval = '1 year' THEN
            v_current_partition_timestamp := date_trunc('year', CURRENT_TIMESTAMP);
    END CASE;
    
    v_current_partition_name := p_parent_table || '_p' || to_char(v_current_partition_timestamp, v_datetime_string);
    v_next_partition_timestamp := v_current_partition_timestamp + v_part_interval::interval;

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            IF NEW.'||v_control||' >= '||quote_literal(v_current_partition_timestamp)||' AND NEW.'||v_control||' < '||quote_literal(v_next_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_current_partition_name||' VALUES (NEW.*); ';
    FOR i IN 1..v_premake LOOP
        v_prev_partition_timestamp := v_current_partition_timestamp - (v_part_interval::interval * i);
        v_next_partition_timestamp := v_current_partition_timestamp + (v_part_interval::interval * i);
        v_final_partition_timestamp := v_next_partition_timestamp + (v_part_interval::interval);
        v_prev_partition_name := p_parent_table || '_p' || to_char(v_prev_partition_timestamp, v_datetime_string);
        v_next_partition_name := p_parent_table || '_p' || to_char(v_next_partition_timestamp, v_datetime_string);

        v_trig_func := v_trig_func ||'
            ELSIF NEW.'||v_control||' >= '||quote_literal(v_prev_partition_timestamp)||' AND NEW.'||v_control||' < '||
                    quote_literal(v_prev_partition_timestamp + v_part_interval::interval)|| ' THEN 
                INSERT INTO '||v_prev_partition_name||' VALUES (NEW.*); 
            ELSIF NEW.'||v_control||' >= '||quote_literal(v_next_partition_timestamp)||' AND NEW.'||v_control||' < '||
                    quote_literal(v_final_partition_timestamp)|| ' THEN 
                INSERT INTO '||v_next_partition_name||' VALUES (NEW.*); ';
    END LOOP;
    v_trig_func := v_trig_func ||' 
            ELSE 
                RETURN NEW; 
            END IF; 
        END IF; 
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for current time interval: '||
            v_current_partition_timestamp||' to '||(v_final_partition_timestamp-'1sec'::interval));
    END IF;

ELSIF v_type = 'time-dynamic' THEN

    v_trig_func := 'CREATE OR REPLACE FUNCTION '||p_parent_table||'_part_trig_func() RETURNS trigger LANGUAGE plpgsql AS $t$ 
        DECLARE
            v_count                 int;
            v_partition_name        text;
            v_partition_timestamp   timestamptz;
            v_schemaname            text;
            v_tablename             text;
        BEGIN 
        IF TG_OP = ''INSERT'' THEN 
            ';
        CASE
            WHEN v_part_interval = '15 mins' THEN 
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''15min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 15.0);';
            WHEN v_part_interval = '30 mins' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||') + 
                    ''30min''::interval * floor(date_part(''minute'', NEW.'||v_control||') / 30.0);';
            WHEN v_part_interval = '1 hour' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''hour'', NEW.'||v_control||');';
             WHEN v_part_interval = '1 day' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''day'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 week' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''week'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 month' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''month'', NEW.'||v_control||');';
            WHEN v_part_interval = '3 months' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''quarter'', NEW.'||v_control||');';
            WHEN v_part_interval = '1 year' THEN
                v_trig_func := v_trig_func||'v_partition_timestamp := date_trunc(''year'', NEW.'||v_control||');';
        END CASE;

        v_trig_func := v_trig_func||'
            v_partition_name := '''||p_parent_table||'_p''|| to_char(v_partition_timestamp, '||quote_literal(v_datetime_string)||');
            v_schemaname := split_part(v_partition_name, ''.'', 1); 
            v_tablename := split_part(v_partition_name, ''.'', 2);
            SELECT count(*) INTO v_count FROM pg_tables WHERE schemaname = v_schemaname AND tablename = v_tablename;
            IF v_count > 0 THEN 
                EXECUTE ''INSERT INTO ''||v_partition_name||'' VALUES($1.*)'' USING NEW;
            ELSE
                RETURN NEW;
            END IF;
        END IF;
        
        RETURN NULL; 
        END $t$;';

    EXECUTE v_trig_func;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Added function for dynamic time table: '||p_parent_table);
    END IF;

ELSE
    RAISE EXCEPTION 'ERROR: Invalid time partitioning type given: %', v_type;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
    EXECUTE 'SELECT set_config(''search_path'','''||v_old_search_path||''',''false'')';
END IF;

EXCEPTION
    WHEN OTHERS THEN
        IF v_jobmon_schema IS NOT NULL THEN
            EXECUTE 'SELECT set_config(''search_path'',''@extschema@,'||v_jobmon_schema||''',''false'')';
            IF v_job_id IS NULL THEN
                v_job_id := add_job('PARTMAN CREATE FUNCTION: '||p_parent_table);
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
 * Function to undo partitioning. 
 * Will actually work on any parent/child table set, not just ones created by pg_partman.
 */
CREATE FUNCTION undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true) RETURNS bigint
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_adv_lock              boolean;
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

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;
-- Stop data going into child tables and stop new id partitions from being made.
v_tablename := substring(p_parent_table from position('.' in p_parent_table)+1);
EXECUTE 'DROP TRIGGER IF EXISTS '||v_tablename||'_part_trig ON '||p_parent_table;
EXECUTE 'DROP FUNCTION IF EXISTS '||p_parent_table||'_part_trig_func()';

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
END IF;

FOR i IN 1..p_batch_count LOOP 
    SELECT inhrelid::regclass INTO v_child_table 
    FROM pg_catalog.pg_inherits 
    WHERE inhparent::regclass = p_parent_table::regclass 
    ORDER BY inhrelid::regclass::text ASC;

    EXIT WHEN v_child_table IS NULL;

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
 * Function to undo time-based partitioning created by this extension
 */
CREATE FUNCTION undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
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

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
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
    ORDER BY inhrelid::regclass::text ASC;

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


/*
 * Function to undo id-based partitioning created by this extension
 */
CREATE FUNCTION undo_partition_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval bigint DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint
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

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
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
    ORDER BY inhrelid::regclass::text ASC;

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
