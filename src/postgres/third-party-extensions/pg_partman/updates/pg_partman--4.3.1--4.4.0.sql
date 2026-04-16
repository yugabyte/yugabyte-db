-- NEW FEATURES
-- ============

-- No longer requires superuser in order to grant privileges directly to child tables (setting inherit_privileges to true or running apply_privileges() directly). Thanks to Stephen Frost for suggesting this fix. This now makes pg_partman no longer require superuser for any operation other than initial installation.

-- BUG FIXES
-- =========

-- Added new config option to allow maintenance to refresh logical replication subscriptions. This is to fix a shortcoming where logical replication is used to replicate partition sets between databases, but not having a way to automatically account for new tables being added on the publisher side. Set the subscription name via the part_config.subscription_refresh column by just setting the name of the subscription. For sub-partitioning, part_config_sub.sub_subscription_refresh is also available. (Github Issue #307)

-- Fix partition_data_time() function (called by partition_data_proc()) to work with epoch time partitioning properly when moving data out of the default partition with native partitioning. Thanks to @sheeju on Github for reporting the issue and providing fix. (Github PR #309)

-- Fixed handling of mixed case/non-standard character object names during maintenance runs with native partitioning on PG11 or greater. Thanks to @sheeju on Github for reporting the issue and providing fix. (Github PR #309)

-- Fixed columns in sub_part_config that were not being kept consistent between configurations for sub-partition tables that were themselves further sub-partitioned. You may possibly have some configurations that are inconsistent due to these columns being missed during previous maintenance runs. The following fix for the consistency check should find this your next maintenance run and throw an error.

-- Fixed consistency check to ensure all parent tables that are part of the same sub-partition set have the same configuration in the sub_part_config table. If you encounter an error about inconsistent data in sub-partition configs after updating to this version, please carefully read the error message that is produced on how to fix it.

-- Fixed broken error message from maintenance runs (record "v_row" has no field "sub_parent") when the above consistency check tried to produce an error message to indicate that the sub-partition configurations had an issue.

ALTER TABLE @extschema@.part_config ADD COLUMN subscription_refresh text;
ALTER TABLE @extschema@.part_config_sub ADD COLUMN sub_subscription_refresh text;

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.check_subpart_sameconfig(text) TO '||array_to_string(array_agg(grantee::text), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'check_subpart_sameconfig'; 

DROP FUNCTION @extschema@.check_subpart_sameconfig(text);

CREATE OR REPLACE VIEW @extschema@.table_privs AS
    SELECT u_grantor.rolname AS grantor,
           grantee.rolname AS grantee,
           nc.nspname AS table_schema,
           c.relname AS table_name,
           c.prtype AS privilege_type
    FROM (
            SELECT oid, relname, relnamespace, relkind, relowner, (aclexplode(coalesce(relacl, acldefault('r', relowner)))).* FROM pg_class
         ) AS c (oid, relname, relnamespace, relkind, relowner, grantor, grantee, prtype, grantable),
         pg_namespace nc,
         pg_roles u_grantor,
         (
           SELECT oid, rolname FROM pg_roles
           UNION ALL
           SELECT 0::oid, 'PUBLIC'
         ) AS grantee (oid, rolname)
    WHERE c.relnamespace = nc.oid
          AND c.relkind IN ('r', 'v', 'p')
          AND c.grantee = grantee.oid
          AND c.grantor = u_grantor.oid
          AND c.prtype IN ('INSERT', 'SELECT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER')
          AND (pg_has_role(u_grantor.oid, 'USAGE')
               OR pg_has_role(grantee.oid, 'USAGE')
               OR grantee.rolname = 'PUBLIC' );



CREATE OR REPLACE FUNCTION @extschema@.partition_data_time(
        p_parent_table text
        , p_batch_count int DEFAULT 1
        , p_batch_interval interval DEFAULT NULL
        , p_lock_wait numeric DEFAULT 0
        , p_order text DEFAULT 'ASC'
        , p_analyze boolean DEFAULT true
        , p_source_table text DEFAULT NULL)
    RETURNS bigint
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_control                   text;
v_control_type              text;
v_datetime_string           text;
v_current_partition_name    text;
v_default_exists            boolean;
v_default_schemaname        text;
v_default_tablename         text;
v_epoch                     text;
v_last_partition            text;
v_lock_iter                 int := 1;
v_lock_obtained             boolean := FALSE;
v_max_partition_timestamp   timestamptz;
v_min_partition_timestamp   timestamptz;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_tablename          text;
v_parent_tablename_real     text;
v_partition_expression      text;
v_partition_interval        interval;
v_partition_suffix          text;
v_partition_timestamp       timestamptz[];
v_partition_type            text;
v_source_schemaname         text;
v_source_tablename          text;
v_rowcount                  bigint;
v_sql                       text;
v_start_control             timestamptz;
v_total_rows                bigint := 0;

BEGIN
/*
 * Populate the child table(s) of a time-based partition set with old data from the original parent
 */

SELECT partition_type
    , partition_interval::interval
    , control
    , datetime_string
    , epoch
INTO v_partition_type
    , v_partition_interval
    , v_control
    , v_datetime_string
    , v_epoch
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table:  %', p_parent_table;
END IF;

SELECT schemaname, tablename INTO v_source_schemaname, v_source_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- Preserve real parent tablename for use below
v_parent_tablename := v_source_tablename;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_source_schemaname, v_source_tablename, v_control);

IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

-- Replace the parent variables with the source variables if using source table for child table data
IF p_source_table IS NOT NULL THEN
    -- Set source table to user given source table instead of parent table
    v_source_schemaname := NULL;
    v_source_tablename := NULL;

    SELECT schemaname, tablename INTO v_source_schemaname, v_source_tablename
    FROM pg_catalog.pg_tables
    WHERE schemaname = split_part(p_source_table, '.', 1)::name
    AND tablename = split_part(p_source_table, '.', 2)::name;

    IF v_source_tablename IS NULL THEN
        RAISE EXCEPTION 'Given source table does not exist in system catalogs: %', p_source_table;
    END IF;


ELSIF v_partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN

    IF p_batch_interval IS NOT NULL AND p_batch_interval != v_partition_interval THEN
        -- This is true because all data for a given child table must be moved out of the default partition before the child table can be created.
        -- So cannot create the child table when only some of the data has been moved out of the default partition.
        RAISE EXCEPTION 'Custom intervals are not allowed when moving data out of the DEFAULT partition in a native set. Please leave p_interval/p_batch_interval parameters unset or NULL to allow use of partition set''s default partitioning interval.';
    END IF;
    -- Set source table to default table if PG11+, p_source_table is not set, and it exists
    -- Otherwise just return with a DEBUG that no data source exists
    v_sql := format('SELECT n.nspname::text, c.relname::text FROM
        pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE h.inhparent = ''%I.%I''::regclass
        AND pg_get_expr(relpartbound, c.oid) = ''DEFAULT'''
        , v_source_schemaname
        , v_source_tablename);

    EXECUTE v_sql INTO v_default_schemaname, v_default_tablename;
    IF v_default_tablename IS NOT NULL THEN
        v_source_schemaname := v_default_schemaname;
        v_source_tablename := v_default_tablename;

        v_default_exists := true;
        EXECUTE format ('CREATE TEMP TABLE IF NOT EXISTS partman_temp_data_storage (LIKE %I.%I INCLUDING INDEXES) ON COMMIT DROP', v_source_schemaname, v_source_tablename);
    ELSE
        RAISE DEBUG 'No default table found when partition_data_id() was called';
        RETURN v_total_rows;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_batch_interval IS NULL OR p_batch_interval > v_partition_interval THEN
    p_batch_interval := v_partition_interval;
END IF;

SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;

FOR i IN 1..p_batch_count LOOP

    IF p_order = 'ASC' THEN
        EXECUTE format('SELECT min(%s) FROM ONLY %I.%I', v_partition_expression, v_source_schemaname, v_source_tablename) INTO v_start_control;
    ELSIF p_order = 'DESC' THEN
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_source_schemaname, v_source_tablename) INTO v_start_control;
    ELSE
        RAISE EXCEPTION 'Invalid value for p_order. Must be ASC or DESC';
    END IF;

    IF v_start_control IS NULL THEN
        EXIT;
    END IF;

    IF v_partition_type = 'partman' THEN
        CASE
            WHEN v_partition_interval = '15 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '15min'::interval * floor(date_part('minute', v_start_control) / 15.0);
            WHEN v_partition_interval = '30 mins' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control) + 
                    '30min'::interval * floor(date_part('minute', v_start_control) / 30.0);
            WHEN v_partition_interval = '1 hour' THEN
                v_min_partition_timestamp := date_trunc('hour', v_start_control);
            WHEN v_partition_interval = '1 day' THEN
                v_min_partition_timestamp := date_trunc('day', v_start_control);
            WHEN v_partition_interval = '1 week' THEN
                v_min_partition_timestamp := date_trunc('week', v_start_control);
            WHEN v_partition_interval = '1 month' THEN
                v_min_partition_timestamp := date_trunc('month', v_start_control);
            WHEN v_partition_interval = '3 months' THEN
                v_min_partition_timestamp := date_trunc('quarter', v_start_control);
            WHEN v_partition_interval = '1 year' THEN
                v_min_partition_timestamp := date_trunc('year', v_start_control);
        END CASE;
    ELSIF v_partition_type IN ('time-custom', 'native') THEN
        SELECT child_start_time INTO v_min_partition_timestamp FROM @extschema@.show_partition_info(v_source_schemaname||'.'||v_last_partition
            , v_partition_interval::text
            , p_parent_table);
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        LOOP
            IF v_start_control >= v_min_partition_timestamp AND v_start_control < v_max_partition_timestamp THEN
                EXIT;
            ELSE
                BEGIN
                    IF v_start_control >= v_max_partition_timestamp THEN
                        -- Keep going forward in time, checking if child partition time interval encompasses the current v_start_control value
                        v_min_partition_timestamp := v_max_partition_timestamp;
                        v_max_partition_timestamp := v_max_partition_timestamp + v_partition_interval;

                    ELSE
                        -- Keep going backwards in time, checking if child partition time interval encompasses the current v_start_control value
                        v_max_partition_timestamp := v_min_partition_timestamp;
                        v_min_partition_timestamp := v_min_partition_timestamp - v_partition_interval;
                    END IF;
                EXCEPTION WHEN datetime_field_overflow THEN
                    RAISE EXCEPTION 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
                        Unable to create partition with interval before timestamp % ', v_min_partition_timestamp;
                END;
            END IF;
        END LOOP;

    END IF;

    v_partition_timestamp := ARRAY[v_min_partition_timestamp];
    IF p_order = 'ASC' THEN
        -- Ensure batch interval given as parameter doesn't cause maximum to overflow the current partition maximum
        IF (v_start_control + p_batch_interval) >= (v_min_partition_timestamp + v_partition_interval) THEN
            v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        ELSE
            v_max_partition_timestamp := v_start_control + p_batch_interval;
        END IF;
    ELSIF p_order = 'DESC' THEN
        -- Must be greater than max value still in parent table since query below grabs < max
        v_max_partition_timestamp := v_min_partition_timestamp + v_partition_interval;
        -- Ensure batch interval given as parameter doesn't cause minimum to underflow current partition minimum
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
                EXECUTE format('SELECT * FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L FOR UPDATE NOWAIT'
                    , v_source_schemaname
                    , v_source_tablename
                    , v_partition_expression
                    , v_min_partition_timestamp
                    , v_max_partition_timestamp);
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

    -- This suffix generation code is in create_partition_time() as well
    v_partition_suffix := to_char(v_min_partition_timestamp, v_datetime_string);
    v_current_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF v_default_exists THEN
        -- Child tables cannot be created in native partitioning if data that belongs to it exists in the default
        -- Have to move data out to temporary location, create child table, then move it back

        -- Temp table created above to avoid excessive temp creation in loop
        EXECUTE format('WITH partition_data AS (
                DELETE FROM %1$I.%2$I WHERE %3$s >= %4$L AND %3$s < %5$L RETURNING *)
            INSERT INTO partman_temp_data_storage SELECT * FROM partition_data'
            , v_source_schemaname
            , v_source_tablename
            , v_partition_expression
            , v_min_partition_timestamp
            , v_max_partition_timestamp);

        PERFORM @extschema@.create_partition_time(p_parent_table, v_partition_timestamp, p_analyze);

        EXECUTE format('WITH partition_data AS (
                DELETE FROM partman_temp_data_storage RETURNING *)
            INSERT INTO %I.%I SELECT * FROM partition_data'
            , v_source_schemaname
            , v_current_partition_name);

    ELSE

        PERFORM @extschema@.create_partition_time(p_parent_table, v_partition_timestamp, p_analyze);

        EXECUTE format('WITH partition_data AS (
                            DELETE FROM ONLY %I.%I WHERE %s >= %L AND %3$s < %5$L RETURNING *)
                         INSERT INTO %I.%I SELECT * FROM partition_data'
                            , v_source_schemaname
                            , v_source_tablename
                            , v_partition_expression
                            , v_min_partition_timestamp
                            , v_max_partition_timestamp
                            , v_source_schemaname
                            , v_current_partition_name);
    END IF;

    GET DIAGNOSTICS v_rowcount = ROW_COUNT;
    v_total_rows := v_total_rows + v_rowcount;
    IF v_rowcount = 0 THEN
        EXIT;
    END IF;

END LOOP; 

IF v_partition_type IN ('partman', 'time-custom') THEN
    PERFORM @extschema@.create_function_time(p_parent_table);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_total_rows;

END
$$;


CREATE OR REPLACE FUNCTION @extschema@.run_maintenance(p_parent_table text DEFAULT NULL, p_analyze boolean DEFAULT NULL, p_jobmon boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS void 
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_adv_lock                      boolean;
v_analyze                       boolean;
v_check_subpart                 int;
v_control_type                  text;
v_create_count                  int := 0;
v_current_partition             text;
v_current_partition_id          bigint;
v_current_partition_timestamp   timestamptz;
v_default_tablename             text;
v_drop_count                    int := 0;
v_is_default                    text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_created        boolean;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_max_id_parent                 bigint;
v_max_time_default               timestamptz;
v_new_search_path               text := '@extschema@,pg_temp';
v_next_partition_id             bigint;
v_next_partition_timestamp      timestamptz;
v_old_search_path               text;
v_parent_exists                 text;
v_parent_oid                    oid;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_expression          text;
v_premade_count                 int;
v_premake_id_max                bigint;
v_premake_id_min                bigint;
v_premake_timestamp_min         timestamptz;
v_premake_timestamp_max         timestamptz;
v_row                           record;
v_row_max_id                    record;
v_row_max_time                  record;
v_row_sub                       record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_id_max                    bigint;
v_sub_id_max_suffix             bigint;
v_sub_id_min                    bigint;
v_sub_parent                    text;
v_sub_refresh_done              text[];
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_max_suffix      timestamptz;
v_sub_timestamp_min             timestamptz;
v_tablename                     text;
v_tables_list_sql               text;

BEGIN
/*
 * Function to manage pre-creation of the next partitions in a set.
 * Also manages dropping old partitions if the retention option is set.
 * If p_parent_table is passed, will only run run_maintenance() on that one table (no matter what the configuration table may have set for it)
 * Otherwise, will run on all tables in the config table with p_automatic_maintenance() set to true.
 * For large partition sets, running analyze can cause maintenance to take longer than expected. Can set p_analyze to false to avoid a forced analyze run on PG versions before 11. 11+ does not analyze by default anymore.
 * Be aware that constraint exclusion may not work properly until an analyze on the partition set is run. 
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman maintenance already running.';
    RETURN;
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
    v_job_id := add_job('PARTMAN RUN MAINTENANCE');
    v_step_id := add_step(v_job_id, 'Running maintenance loop');
END IF;

v_tables_list_sql := 'SELECT parent_table
                , partition_type
                , partition_interval
                , control
                , premake
                , undo_in_progress
                , sub_partition_set_full
                , epoch
                , infinite_time_partitions
                , retention
                , subscription_refresh
            FROM @extschema@.part_config
            WHERE undo_in_progress = false';

IF p_parent_table IS NULL THEN
    v_tables_list_sql := v_tables_list_sql || ' AND automatic_maintenance = ''on''';
ELSE
    v_tables_list_sql := v_tables_list_sql || format(' AND parent_table = %L', p_parent_table);
END IF;

FOR v_row IN EXECUTE v_tables_list_sql
LOOP

    CONTINUE WHEN v_row.undo_in_progress;

    -- When sub-partitioning, retention may drop tables that were already put into the query loop values. 
    -- Check if they still exist in part_config before continuing
    v_parent_exists := NULL;
    SELECT parent_table INTO v_parent_exists FROM @extschema@.part_config WHERE parent_table = v_row.parent_table;
    RAISE DEBUG 'Parent table possibly removed from part_config by retenion';
    CONTINUE WHEN v_parent_exists IS NULL;
    
    -- Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
    -- Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
    -- This way at least provides a consistent way to check that I know will run. If anyone can get a working constraint/trigger, please help!
    SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = v_row.parent_table;
    IF v_sub_parent IS NOT NULL THEN
        SELECT count(*) INTO v_check_subpart FROM @extschema@.check_subpart_sameconfig(v_row.parent_table);
        IF v_check_subpart > 1 THEN
            RAISE EXCEPTION 'Inconsistent data in part_config_sub table. Sub-partition tables that are themselves sub-partitions cannot have differing configuration values among their siblings. 
            Run this query: "SELECT * FROM @extschema@.check_subpart_sameconfig(''%'');" This should only return a single row or nothing. 
            If multiple rows are returned, the results are differing configurations in the part_config_sub table for children of the given parent. 
            Determine the child tables of the given parent and look up their entries based on the "part_config_sub.sub_parent" column. 
            Update the differing values to be consistent for your desired values.', v_row.parent_table;
        END IF;
    END IF;

    -- Shouldn't need to analyze tables for most statistics for native sets on PG11+ by default anymore
    IF p_analyze IS NULL THEN
        IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
            v_analyze := false;
        ELSE
            v_analyze := true;
        END IF;
    END IF;

    SELECT n.nspname, c.relname, c.oid
    INTO v_parent_schema, v_parent_tablename, v_parent_oid
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_row.parent_table, '.', 1)::name
    AND c.relname = split_part(v_row.parent_table, '.', 2)::name;

    -- Used below to see if there's any data in the parent (<=PG10) or default (PG11+) child table.
    IF v_row.partition_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        -- Always returns the default partition first if it exists
        SELECT partition_tablename INTO v_default_tablename 
        FROM @extschema@.show_partitions(v_row.parent_table, p_include_default := true) LIMIT 1;

        SELECT pg_get_expr(relpartbound, v_parent_oid) INTO v_is_default 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n on c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema
        AND c.relname = v_default_tablename;

        IF v_is_default != 'DEFAULT' THEN
            v_default_tablename := v_parent_tablename;
        END IF;
    ELSE
        v_default_tablename := v_parent_tablename;
    END IF;

    SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_row.control);

    v_partition_expression := CASE
        WHEN v_row.epoch = 'seconds' THEN format('to_timestamp(%I)', v_row.control)
        WHEN v_row.epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_row.control)
        ELSE format('%I', v_row.control)
    END;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: v_partition_expression: %', v_partition_expression;
    END IF;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_row.parent_table, 'DESC') LIMIT 1;
    IF p_debug THEN
        RAISE NOTICE 'run_maint: parent_table: %, v_last_partition: %', v_row.parent_table, v_last_partition;
    END IF;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_row.epoch <> 'none') THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_time(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        SELECT child_start_time INTO v_last_partition_timestamp 
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        IF v_row.infinite_time_partitions IS TRUE THEN
            -- Set it to "now" so new partitions continue to be created
            -- For infinite_time_partitions, don't bother getting the max value in the partitions
            v_current_partition_timestamp = CURRENT_TIMESTAMP;
        ELSE 
             FOR v_row_max_time IN
               SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
            LOOP
                EXECUTE format('SELECT max(%s)::text FROM %I.%I'
                                    , v_partition_expression
                                    , v_row_max_time.partition_schemaname
                                    , v_row_max_time.partition_tablename
                                ) INTO v_current_partition_timestamp;

                IF v_current_partition_timestamp IS NOT NULL THEN
                    SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_timestamp::text);
                    EXIT;
                END IF;
            END LOOP;
        END IF; -- end infinite time check

        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in default, but new tables will be created
        EXECUTE format('SELECT max(%s) FROM ONLY %I.%I', v_partition_expression, v_parent_schema, v_default_tablename) INTO v_max_time_default;
        IF p_debug THEN
            RAISE NOTICE 'run_maint: v_current_partition_timestamp: %, v_max_time_default: %', v_current_partition_timestamp, v_max_time_default;
        END IF;
        IF v_current_partition_timestamp IS NULL AND v_max_time_default IS NULL THEN 
            -- Partition set is completely empty and infinite time partitions not set
            -- Nothing to do
            CONTINUE;
        END IF;
        IF v_current_partition_timestamp IS NULL OR (v_max_time_default > v_current_partition_timestamp) THEN
            SELECT suffix_timestamp INTO v_current_partition_timestamp FROM @extschema@.show_partition_name(v_row.parent_table, v_max_time_default::text);
        END IF;

        -- If this is a subpartition, determine if the last child table has been made. If so, mark it as full so future maintenance runs can skip it
        SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'time');
        IF v_sub_timestamp_max IS NOT NULL THEN
            SELECT suffix_timestamp INTO v_sub_timestamp_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_timestamp_max::text);
            IF v_sub_timestamp_max_suffix = v_last_partition_timestamp THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        -- Check and see how many premade partitions there are.
        v_premade_count = round(EXTRACT('epoch' FROM age(v_last_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        v_next_partition_timestamp := v_last_partition_timestamp;
        IF p_debug THEN
            RAISE NOTICE 'run_maint before loop: current_partition_timestamp: %, v_premade_count: %, v_sub_timestamp_min: %, v_sub_timestamp_max: %'
                , v_current_partition_timestamp 
                , v_premade_count
                , v_sub_timestamp_min
                , v_sub_timestamp_max;
        END IF;
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed
        WHILE (v_premade_count < v_row.premake) LOOP
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_timestamp: %', v_row.parent_table, v_premade_count, v_next_partition_timestamp;
            END IF;
            IF v_next_partition_timestamp < v_sub_timestamp_min OR v_next_partition_timestamp > v_sub_timestamp_max THEN
                -- With subpartitioning, no need to run if the timestamp is not in the parent table's range
                EXIT;
            END IF;
            BEGIN
                v_next_partition_timestamp := v_next_partition_timestamp + v_row.partition_interval::interval;
            EXCEPTION WHEN datetime_field_overflow THEN
                v_premade_count := v_row.premake; -- do this so it can exit the premake check loop and continue in the outer for loop 
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
                    PERFORM update_step(v_step_overflow_id, 'CRITICAL', format('Child partition creation skipped for parent table: %s', v_partition_time));
                END IF;
                RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. Child partition creation skipped for parent table %', v_row.parent_table;
                CONTINUE;
            END;

            v_last_partition_created := @extschema@.create_partition_time(v_row.parent_table
                                                        , ARRAY[v_next_partition_timestamp]
                                                        , v_analyze
                                                        , p_debug := p_debug); 
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_time(v_row.parent_table, v_job_id);
                END IF;
            END IF;

            v_premade_count = round(EXTRACT('epoch' FROM age(v_next_partition_timestamp, v_current_partition_timestamp)) / EXTRACT('epoch' FROM v_row.partition_interval::interval));
        END LOOP;

    ELSIF v_control_type = 'id' THEN

        -- Run retention if needed
        IF v_row.retention IS NOT NULL THEN
            v_drop_count := v_drop_count + @extschema@.drop_partition_id(v_row.parent_table);   
            IF v_drop_count > 0 AND v_row.partition_type <> 'native' THEN
                PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
            END IF;
        END IF;

        IF v_row.sub_partition_set_full THEN CONTINUE; END IF;

        -- Loop through child tables starting from highest to get current max value in partition set
        -- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.

        FOR v_row_max_id IN
            SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(v_row.parent_table, 'DESC')
        LOOP
            EXECUTE format('SELECT max(%I)::text FROM %I.%I'
                            , v_row.control
                            , v_row_max_id.partition_schemaname
                            , v_row_max_id.partition_tablename) INTO v_current_partition_id;
            IF v_current_partition_id IS NOT NULL THEN
                SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_current_partition_id::text);
                EXIT;
            END IF;
        END LOOP;
        -- Check for values in the parent/default table. If they are there and greater than all child values, use that instead
        -- This allows maintenance to continue working properly if there is a large gap in data insertion. Data will remain in parent, but new tables will be created
        EXECUTE format('SELECT max(%I) FROM ONLY %I.%I', v_row.control, v_parent_schema, v_default_tablename) INTO v_max_id_parent;
        IF v_max_id_parent > v_current_partition_id THEN
            SELECT suffix_id INTO v_current_partition_id FROM @extschema@.show_partition_name(v_row.parent_table, v_max_id_parent::text);
        END IF;
        IF v_current_partition_id IS NULL THEN
            -- Partition set is completely empty. Nothing to do
            CONTINUE;
        END IF;

        SELECT child_start_id INTO v_last_partition_id
            FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_row.partition_interval, v_row.parent_table);
        -- Determine if this table is a child of a subpartition parent. If so, get limits to see if run_maintenance even needs to run for it.
        SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(v_row.parent_table, 'id');
        IF v_sub_id_max IS NOT NULL THEN
            SELECT suffix_id INTO v_sub_id_max_suffix FROM @extschema@.show_partition_name(v_row.parent_table, v_sub_id_max::text);
            IF v_sub_id_max_suffix = v_last_partition_id THEN
                -- Final partition for this set is created. Set full and skip it
                UPDATE @extschema@.part_config SET sub_partition_set_full = true WHERE parent_table = v_row.parent_table;
                CONTINUE;
            END IF;
        END IF;

        v_next_partition_id := v_last_partition_id;
        v_premade_count := ((v_last_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        -- Loop premaking until config setting is met. Allows it to catch up if it fell behind or if premake changed.
        WHILE (v_premade_count < v_row.premake) LOOP 
            IF p_debug THEN
                RAISE NOTICE 'run_maint: parent_table: %, v_premade_count: %, v_next_partition_id: %', v_row.parent_table, v_premade_count, v_next_partition_id;
            END IF;
            IF v_next_partition_id < v_sub_id_min OR v_next_partition_id > v_sub_id_max THEN
                -- With subpartitioning, no need to run if the id is not in the parent table's range
                EXIT;
            END IF;
            v_next_partition_id := v_next_partition_id + v_row.partition_interval::bigint;
            v_last_partition_created := @extschema@.create_partition_id(v_row.parent_table, ARRAY[v_next_partition_id], v_analyze);
            IF v_last_partition_created THEN
                v_create_count := v_create_count + 1;
                IF v_row.partition_type <> 'native' THEN
                    PERFORM @extschema@.create_function_id(v_row.parent_table, v_job_id);
                END IF;
            END IF;
            v_premade_count := ((v_next_partition_id - v_current_partition_id) / v_row.partition_interval::bigint);
        END LOOP;

    END IF; -- end main IF check for time or id

    -- Refresh subscriptions in order to catch new tables that may have been created in the publication
    -- Keep track of which ones have been refreshed so it doesn't needlessly run more than once
    -- in a single maintenance run
    IF v_row.subscription_refresh IS NOT NULL THEN
        IF v_sub_refresh_done @> ARRAY[v_row.subscription_refresh] THEN
            CONTINUE;
        ELSE
            v_sql = format('ALTER SUBSCRIPTION %I REFRESH PUBLICATION', v_row.subscription_refresh);
            RAISE DEBUG '%', v_sql;
            EXECUTE v_sql;
            PERFORM array_append(v_sub_refresh_done, v_row.subscription_refresh);
        END IF;
    END IF;

END LOOP; -- end of main loop through part_config

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', format('Partition maintenance finished. %s partitons made. %s partitions dropped.', v_create_count, v_drop_count));
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
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
                EXECUTE format('SELECT %I.add_job(''PARTMAN RUN MAINTENANCE'')', v_jobmon_schema) INTO v_job_id;
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


CREATE OR REPLACE FUNCTION @extschema@.create_parent(
    p_parent_table text
    , p_control text
    , p_type text
    , p_interval text
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_automatic_maintenance text DEFAULT 'on' 
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch text DEFAULT 'none' 
    , p_upsert text DEFAULT ''
    , p_publications text[] DEFAULT NULL
    , p_trigger_return_null boolean DEFAULT true
    , p_template_table text DEFAULT NULL
    , p_jobmon boolean DEFAULT true
    , p_debug boolean DEFAULT false) 
RETURNS boolean 
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_partattrs                     smallint[];
v_base_timestamp                timestamptz;
v_count                         int := 1;
v_control_type                  text;
v_control_exact_type            text;
v_datetime_string               text;
v_default_partition             text;
v_higher_control_type           text;
v_higher_parent_control         text;
v_higher_parent_schema          text := split_part(p_parent_table, '.', 1);
v_higher_parent_table           text := split_part(p_parent_table, '.', 2);
v_id_interval                   bigint;
v_inherit_privileges            boolean := false;
v_job_id                        bigint;
v_jobmon_schema                 text;
v_last_partition_created        boolean;
v_max                           bigint;
v_native_sub_control            text;
v_notnull                       boolean;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_owner                  text;
v_parent_partition_id           bigint;
v_parent_partition_timestamp    timestamptz;
v_parent_schema                 text;
v_parent_tablename              text;
v_parent_tablespace             text;
v_part_col                      text;
v_part_type                     text;
v_partition_time                timestamptz;
v_partition_time_array          timestamptz[];
v_partition_id_array            bigint[];
v_partstrat                     char;
v_publication_exists            text;
v_row                           record;
v_sql                           text;
v_start_time                    timestamptz;
v_starting_partition_id         bigint;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_parent                    text;
v_success                       boolean := false;
v_template_schema               text;
v_template_tablename            text;
v_time_interval                 interval;
v_top_datetime_string           text;
v_top_parent_schema             text := split_part(p_parent_table, '.', 1);
v_top_parent_table              text := split_part(p_parent_table, '.', 2);
v_unlogged                      char;

BEGIN
/*
 * Function to turn a table into the parent of a partition set
 */

IF position('.' in p_parent_table) = 0  THEN
    RAISE EXCEPTION 'Parent table must be schema qualified';
END IF;

IF p_upsert <> '' THEN
    IF current_setting('server_version_num')::int < 90500 THEN
        RAISE EXCEPTION 'INSERT ... ON CONFLICT (UPSERT) feature is only supported in PostgreSQL 9.5 and later';
    END IF;
    IF p_type = 'native' THEN
        RAISE EXCEPTION 'Native partitioning does not currently support upsert. Use pg_partman''s partitioning methods instead if this is required';
    END IF;
END IF;

SELECT n.nspname, c.relname, t.spcname, c.relpersistence
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace, v_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_parent_table;
    END IF;
    
SELECT attnotnull INTO v_notnull 
FROM pg_catalog.pg_attribute a
JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE c.relname = v_parent_tablename::name
AND n.nspname = v_parent_schema::name
AND a.attname = p_control::name;
    IF p_type <> 'native' AND (v_notnull = false OR v_notnull IS NULL) THEN
        RAISE EXCEPTION 'Control column given (%) for parent table (%) does not exist or must be set to NOT NULL', p_control, p_parent_table;
    END IF;

SELECT general_type, exact_type INTO v_control_type, v_control_exact_type
FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, p_control);

IF v_control_type IS NULL THEN
    RAISE EXCEPTION 'pg_partman only supports partitioning of data types that are integer or date/timestamp. Supplied column is of type %', v_control_exact_type;
END IF;

IF (p_epoch <> 'none' AND v_control_type <> 'id') THEN
    RAISE EXCEPTION 'p_epoch can only be used with an integer based control column and does not work for native partitioning';
END IF;


IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type for pg_partman', p_type;
END IF;

IF p_type = 'native' THEN

    IF current_setting('server_version_num')::int < 100000 THEN
        RAISE EXCEPTION 'Native partitioning only available in PostgreSQL versions 10.0+';
    END IF;
    -- Check if given parent table has been already set up as a partitioned table and is ranged
    SELECT p.partstrat, partattrs INTO v_partstrat, v_partattrs
    FROM pg_catalog.pg_partitioned_table p
    JOIN pg_catalog.pg_class c ON p.partrelid = c.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name 
    AND c.relname = v_parent_tablename::name;

    IF v_partstrat <> 'r' OR v_partstrat IS NULL THEN
        RAISE EXCEPTION 'When using native partitioning, you must have created the given parent table as ranged (not list) partitioned already. Ex: CREATE TABLE ... PARITIONED BY RANGE ...)';
    END IF;

    IF array_length(v_partattrs, 1) > 1 THEN
        RAISE NOTICE 'pg_partman only supports single column native partitioning at this time. Found % columns in given parent definition.', array_length(v_partattrs, 1);
    END IF;

    SELECT a.attname, t.typname
    INTO v_part_col, v_part_type
    FROM pg_attribute a
    JOIN pg_class c ON a.attrelid = c.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    JOIN pg_type t ON a.atttypid = t.oid
    WHERE n.nspname = v_parent_schema::name
    AND c.relname = v_parent_tablename::name
    AND attnum IN (SELECT unnest(partattrs) FROM pg_partitioned_table p WHERE a.attrelid = p.partrelid);

    IF p_control <> v_part_col OR v_control_exact_type <> v_part_type THEN
        RAISE EXCEPTION 'Control column and type given in arguments (%, %) does not match the control column and type of the given native partition set (%, %)', p_control, v_control_exact_type, v_part_col, v_part_type;
    END IF;

    -- Check that control column is a usable type for pg_partman.
    IF v_control_type NOT IN ('time', 'id') THEN
        RAISE EXCEPTION 'Only date/time or integer types are allowed for the control column with native partitioning.';
    END IF;

    -- Table to handle properties not natively inherited yet (indexes, fks, etc)
    IF p_template_table IS NULL THEN
        v_template_schema := '@extschema@';
        v_template_tablename := @extschema@.check_name_length('template_'||v_parent_schema||'_'||v_parent_tablename);
        EXECUTE format('CREATE TABLE IF NOT EXISTS %I.%I (LIKE %I.%I)', '@extschema@', v_template_tablename, v_parent_schema, v_parent_tablename);

        SELECT pg_get_userbyid(c.relowner) INTO v_parent_owner 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema::name 
        AND c.relname = v_parent_tablename::name;

        EXECUTE format('ALTER TABLE %I.%I OWNER TO %I'
                , '@extschema@' 
                , v_template_tablename 
                , v_parent_owner);
    ELSE
        SELECT n.nspname, c.relname INTO v_template_schema, v_template_tablename
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = split_part(p_template_table, '.', 1)::name
        AND c.relname = split_part(p_template_table, '.', 2)::name;
            IF v_template_tablename IS NULL THEN
                RAISE EXCEPTION 'Unable to find given template table in system catalogs (%). Please create template table first or leave parameter NULL to have a default one created for you.', p_parent_table;
            END IF;
    END IF;

ELSE -- if not native 

    IF current_setting('server_version_num')::int >= 100000 THEN
        SELECT p.partstrat INTO v_partstrat
        FROM pg_catalog.pg_partitioned_table p
        JOIN pg_catalog.pg_class c ON p.partrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname = v_parent_schema::name 
        AND c.relname = v_parent_tablename::name;
    END IF;

    IF v_partstrat IS NOT NULL THEN
        RAISE EXCEPTION 'Given parent table has been set up with native partitioning therefore cannot be used with pg_partman''s other partitioning types. Either recreate table non-native or set the type argument to ''native''';
    END IF;

END IF; -- end if "native" check


IF p_publications IS NOT NULL THEN
    IF current_setting('server_version_num')::int < 100000 THEN
        RAISE EXCEPTION 'p_publications argument not null but CREATE PUBLICATION is only available in PostgreSQL versions 10.0+';
    END IF;
    IF p_publications = '{}' THEN
        RAISE EXCEPTION 'p_publications cannot be an empty set';
    END IF;
    FOR v_row IN 
        SELECT unnest(p_publications) AS pubname
    LOOP
        SELECT pubname INTO v_publication_exists FROM pg_catalog.pg_publication where pubname = v_row.pubname::name;
        IF v_publication_exists IS NULL THEN
            RAISE EXCEPTION 'Given publication name (%) does not exist in system catalog. Ensure it is created first.', v_row.pubname;
        END IF;
    END LOOP;
END IF;

-- Only inherit parent ownership/privileges on non-native sets by default
-- This is false by default so initial partition set creation doesn't require superuser.
IF p_type = 'native' THEN
    v_inherit_privileges = false;
ELSE
    v_inherit_privileges  = true;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF p_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

EXECUTE format('LOCK TABLE %I.%I IN ACCESS EXCLUSIVE MODE', v_parent_schema, v_parent_tablename);

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN SETUP PARENT: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Creating initial partitions on new parent table: %s', p_parent_table));
END IF;

-- If this parent table has siblings that are also partitioned (subpartitions), ensure this parent gets added to part_config_sub table so future maintenance will subpartition it
-- Just doing in a loop to avoid having to assign a bunch of variables (should only run once, if at all; constraint should enforce only one value.)
FOR v_row IN 
    WITH parent_table AS (
        SELECT h.inhparent AS parent_oid
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON h.inhrelid = c.oid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename::name
        AND n.nspname = v_parent_schema::name
    ), sibling_children AS (
        SELECT i.inhrelid::regclass::text AS tablename 
        FROM pg_inherits i
        JOIN parent_table p ON i.inhparent = p.parent_oid
    )
    -- This column list must be kept consistent between: 
    --   create_parent, check_subpart_sameconfig, create_partition_id, create_partition_time, dump_partitioned_table_definition and table definition
    SELECT DISTINCT sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_automatic_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null
        , sub_template_table
        , sub_inherit_privileges
        , sub_constraint_valid
        , sub_subscription_refresh
    FROM @extschema@.part_config_sub a
    JOIN sibling_children b on a.sub_parent = b.tablename LIMIT 1
LOOP
    INSERT INTO @extschema@.part_config_sub (
        sub_parent
        , sub_partition_type
        , sub_control
        , sub_partition_interval
        , sub_constraint_cols
        , sub_premake
        , sub_inherit_fk
        , sub_retention
        , sub_retention_schema
        , sub_retention_keep_table
        , sub_retention_keep_index
        , sub_automatic_maintenance
        , sub_epoch
        , sub_optimize_trigger
        , sub_optimize_constraint
        , sub_infinite_time_partitions
        , sub_jobmon
        , sub_trigger_exception_handling
        , sub_upsert
        , sub_trigger_return_null
        , sub_template_table
        , sub_inherit_privileges
        , sub_constraint_valid
        , sub_subscription_refresh)
    VALUES (
        p_parent_table
        , v_row.sub_partition_type
        , v_row.sub_control
        , v_row.sub_partition_interval
        , v_row.sub_constraint_cols
        , v_row.sub_premake
        , v_row.sub_inherit_fk
        , v_row.sub_retention
        , v_row.sub_retention_schema
        , v_row.sub_retention_keep_table
        , v_row.sub_retention_keep_index
        , v_row.sub_automatic_maintenance
        , v_row.sub_epoch
        , v_row.sub_optimize_trigger
        , v_row.sub_optimize_constraint
        , v_row.sub_infinite_time_partitions
        , v_row.sub_jobmon
        , v_row.sub_trigger_exception_handling
        , v_row.sub_upsert
        , v_row.sub_trigger_return_null
        , v_row.sub_template_table
        , v_row.sub_inherit_privileges
        , v_row.sub_constraint_valid
        , v_row.sub_subscription_refresh);

    -- Set this equal to sibling configs so that newly created child table 
    -- privileges are set properly below during initial setup.
    -- This setting is special because it applies immediately to the new child 
    -- tables of a given parent, not just during maintenance like most other settings.
    v_inherit_privileges = v_row.sub_inherit_privileges;
END LOOP;

IF v_control_type = 'time' OR (v_control_type = 'id' AND p_epoch <> 'none') THEN

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
            IF p_type <> 'native' THEN
                -- Reset for use as part_config type value below
                p_type = 'time-custom';
            END IF;
            v_time_interval := p_interval::interval;
            IF v_time_interval < '1 second'::interval THEN
                RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
            END IF;
    END CASE;

   -- First partition is either the min premake or p_start_partition
    v_start_time := COALESCE(p_start_partition::timestamptz, CURRENT_TIMESTAMP - (v_time_interval * p_premake));

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
                v_partition_time := (v_base_timestamp + (v_time_interval * v_count))::timestamptz;
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

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , epoch
        , control
        , premake
        , constraint_cols
        , datetime_string
        , automatic_maintenance
        , inherit_fk
        , jobmon 
        , upsert
        , trigger_return_null
        , template_table
        , publications
        , inherit_privileges)
    VALUES (
        p_parent_table
        , p_type
        , v_time_interval
        , p_epoch
        , p_control
        , p_premake
        , p_constraint_cols
        , v_datetime_string
        , p_automatic_maintenance
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null
        , v_template_schema||'.'||v_template_tablename
        , p_publications
        , v_inherit_privileges); 

    RAISE DEBUG 'create_parent: v_partition_time_array: %', v_partition_time_array;

    v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);

    IF v_last_partition_created = false THEN 
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- First see if this parent is a subpartition managed by pg_partman
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname, c.relname 
        INTO v_top_parent_schema, v_top_parent_table 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_top_parent_table IS NOT NULL THEN
            -- If so create the lowest possible partition that is within the boundary of the parent
            SELECT child_start_time INTO v_parent_partition_timestamp FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_schema||'.'||v_top_parent_table);
            IF v_base_timestamp >= v_parent_partition_timestamp THEN
                WHILE v_base_timestamp >= v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp - v_time_interval;
                END LOOP;
                v_base_timestamp := v_base_timestamp + v_time_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_base_timestamp < v_parent_partition_timestamp THEN
                WHILE v_base_timestamp < v_parent_partition_timestamp LOOP
                    v_base_timestamp := v_base_timestamp + v_time_interval;
                END LOOP;
                -- Don't need to remove one since new starting time will fit in top parent interval
            END IF;
            v_partition_time_array := NULL;
            v_partition_time_array := array_append(v_partition_time_array, v_base_timestamp);
            v_last_partition_created := @extschema@.create_partition_time(p_parent_table, v_partition_time_array, false);
        ELSE
            RAISE WARNING 'No child tables created. Check that all child tables did not already exist and may not have been part of partition set. Given parent has still been configured with pg_partman, but may not have expected children. Please review schema and config to confirm things are ok.';

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
                IF v_step_overflow_id IS NOT NULL THEN
                    PERFORM fail_job(v_job_id);
                ELSE
                    PERFORM close_job(v_job_id);
                END IF;
            END IF;

            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

            RETURN v_success;
        END IF; 
    END IF; -- End v_last_partition IF

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Time partitions premade: %s', p_premake));
    END IF;

END IF;

IF v_control_type = 'id' AND p_epoch = 'none' THEN
    v_id_interval := p_interval::bigint;
    IF p_type <> 'native' AND v_id_interval < 10 THEN
        RAISE EXCEPTION 'Interval for serial, non-native partitioning must be greater than or equal to 10';
    END IF;

    -- Check if parent table is a subpartition of an already existing id partition set managed by pg_partman. 
    WHILE v_higher_parent_table IS NOT NULL LOOP -- initially set in DECLARE
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE n.nspname = v_higher_parent_schema::name
            AND c.relname = v_higher_parent_table::name
        ) SELECT n.nspname, c.relname, p.control
        INTO v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_higher_parent_table IS NOT NULL THEN
            SELECT general_type INTO v_higher_control_type
            FROM @extschema@.check_control_type(v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control);
            IF v_higher_control_type <> 'id' THEN
                -- The parent above the p_parent_table parameter is not partitioned by ID
                --   so don't check for max values in parents that aren't partitioned by ID.
                -- This avoids missing child tables in subpartition sets that have differing ID data
                EXIT;
            END IF;
            -- v_top_parent initially set in DECLARE
            v_top_parent_schema := v_higher_parent_schema;
            v_top_parent_table := v_higher_parent_table;
        END IF;
    END LOOP;

    -- If custom start partition is set, use that.
    -- If custom start is not set and there is already data, start partitioning with the highest current value and ensure it's grabbed from highest top parent table
    IF p_start_partition IS NOT NULL THEN
        v_max := p_start_partition::bigint;
    ELSE
        v_sql := format('SELECT COALESCE(max(%I)::bigint, 0) FROM %I.%I LIMIT 1'
                    , p_control
                    , v_top_parent_schema
                    , v_top_parent_table);
        EXECUTE v_sql INTO v_max;
    END IF;

    v_starting_partition_id := v_max - (v_max % v_id_interval);
    FOR i IN 0..p_premake LOOP
        -- Only make previous partitions if ID value is less than the starting value and positive (and custom start partition wasn't set)
        IF p_start_partition IS NULL AND 
            (v_starting_partition_id - (v_id_interval*i)) > 0 AND 
            (v_starting_partition_id - (v_id_interval*i)) < v_starting_partition_id 
        THEN
            v_partition_id_array = array_append(v_partition_id_array, (v_starting_partition_id - v_id_interval*i));
        END IF; 
        v_partition_id_array = array_append(v_partition_id_array, (v_id_interval*i) + v_starting_partition_id);
    END LOOP;

    INSERT INTO @extschema@.part_config (
        parent_table
        , partition_type
        , partition_interval
        , control
        , premake
        , constraint_cols
        , automatic_maintenance
        , inherit_fk
        , jobmon
        , upsert
        , trigger_return_null
        , template_table
        , publications
        , inherit_privileges)
    VALUES (
        p_parent_table
        , p_type
        , v_id_interval
        , p_control
        , p_premake
        , p_constraint_cols
        , p_automatic_maintenance 
        , p_inherit_fk
        , p_jobmon
        , p_upsert
        , p_trigger_return_null
        , v_template_schema||'.'||v_template_tablename
        , p_publications
        , v_inherit_privileges); 

    v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);

    IF v_last_partition_created = false THEN
        -- This can happen with subpartitioning when future or past partitions prevent child creation because they're out of range of the parent
        -- See if it's actually a subpartition of a parent id partition
        WITH top_oid AS (
            SELECT i.inhparent AS top_parent_oid
            FROM pg_catalog.pg_inherits i
            JOIN pg_catalog.pg_class c ON c.oid = i.inhrelid
            JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relname = v_parent_tablename::name
            AND n.nspname = v_parent_schema::name
        ) SELECT n.nspname||'.'||c.relname
        INTO v_top_parent_table
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_top_parent_table IS NOT NULL THEN
            -- Create the lowest possible partition that is within the boundary of the parent
             SELECT child_start_id INTO v_parent_partition_id FROM @extschema@.show_partition_info(p_parent_table, p_parent_table := v_top_parent_table);
            IF v_starting_partition_id >= v_parent_partition_id THEN
                WHILE v_starting_partition_id >= v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id - v_id_interval;
                END LOOP;
                v_starting_partition_id := v_starting_partition_id + v_id_interval; -- add one back since while loop set it one lower than is needed
            ELSIF v_starting_partition_id < v_parent_partition_id THEN
                WHILE v_starting_partition_id < v_parent_partition_id LOOP
                    v_starting_partition_id := v_starting_partition_id + v_id_interval;
                END LOOP;
                -- Don't need to remove one since new starting id will fit in top parent interval
            END IF;
            v_partition_id_array = NULL;
            v_partition_id_array = array_append(v_partition_id_array, v_starting_partition_id);
            v_last_partition_created := @extschema@.create_partition_id(p_parent_table, v_partition_id_array, false);
        ELSE
            -- Currently unknown edge case if code gets here
            RAISE WARNING 'No child tables created. Check that all child tables did not already exist and may not have been part of partition set. Given parent has still been configured with pg_partman, but may not have expected children. Please review schema and config to confirm things are ok.';
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
                IF v_step_overflow_id IS NOT NULL THEN
                    PERFORM fail_job(v_job_id);
                ELSE
                    PERFORM close_job(v_job_id);
                END IF;
            END IF;

            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

            RETURN v_success;
        END IF;
    END IF; -- End v_last_partition_created IF

END IF; -- End IF id

IF p_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
    -- Add default partition to native sets in PG11+

    v_default_partition := @extschema@.check_name_length(v_parent_tablename, '_default', FALSE);
    v_sql := 'CREATE'; 

    -- Left this here as reminder to revisit once native figures out how it is handling changing unlogged stats
    -- Currently handed via template table below
    /* 
    IF v_unlogged = 'u' THEN
         v_sql := v_sql ||' UNLOGGED';
    END IF;
    */

    -- Same INCLUDING list is used in create_partition_*(). INDEXES is handled when partition is attached if it's supported.
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS '
        , v_parent_schema, v_default_partition, v_parent_schema, v_parent_tablename);
    IF current_setting('server_version_num')::int >= 120000 THEN
        v_sql := v_sql || ' INCLUDING GENERATED ';
    END IF;
    v_sql := v_sql || ')';
    EXECUTE v_sql;
    v_sql := format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I DEFAULT'
        , v_parent_schema, v_parent_tablename, v_parent_schema, v_default_partition);
    EXECUTE v_sql;

    IF current_setting('server_version_num')::int >= 120000 AND v_parent_tablespace IS NOT NULL THEN
        -- Tablespace managed via inherit_template_properties() call below if PG11 or earliser
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_default_partition, v_parent_tablespace);
    END IF;

    -- Manage template inherited properies
    PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_default_partition);

END IF;

IF p_type <> 'native' THEN
    IF v_jobmon_schema IS NOT NULL  THEN
        v_step_id := add_step(v_job_id, 'Creating partition function');
    END IF;
    IF v_control_type = 'time' OR (v_control_type = 'id' AND p_epoch <> 'none') THEN
        PERFORM @extschema@.create_function_time(p_parent_table, v_job_id);
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Time function created');
        END IF;
    ELSIF v_control_type = 'id' THEN
        PERFORM @extschema@.create_function_id(p_parent_table, v_job_id);  
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'ID function created');
        END IF;
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating partition trigger');
    END IF;
    PERFORM @extschema@.create_trigger(p_parent_table);
END IF; -- end native check


IF v_jobmon_schema IS NOT NULL THEN
    PERFORM update_step(v_step_id, 'OK', 'Done');
    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

v_success := true;

RETURN v_success;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE PARENT: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''Partition creation for table '||p_parent_table||' failed'')', v_jobmon_schema, v_job_id, p_parent_table) INTO v_step_id;
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


CREATE FUNCTION @extschema@.check_subpart_sameconfig(p_parent_table text) 
    RETURNS TABLE (sub_partition_type text
        , sub_control text
        , sub_partition_interval text
        , sub_constraint_cols text[]
        , sub_premake int
        , sub_optimize_trigger int
        , sub_optimize_constraint int
        , sub_epoch text
        , sub_inherit_fk boolean
        , sub_retention text
        , sub_retention_schema text
        , sub_retention_keep_table boolean
        , sub_retention_keep_index boolean
        , sub_infinite_time_partitions boolean
        , sub_automatic_maintenance text
        , sub_jobmon boolean
        , sub_trigger_exception_handling boolean
        , sub_upsert text
        , sub_trigger_return_null boolean
        , sub_template_table text
        , sub_inherit_privileges boolean
        , sub_constraint_valid boolean
        , sub_subscription_refresh text)
    LANGUAGE sql STABLE
    SET search_path = @extschema@,pg_temp
AS $$
/*
 * Check for consistent data in part_config_sub table. Was unable to get this working properly as either a constraint or trigger. 
 * Would either delay raising an error until the next write (which I cannot predict) or disallow future edits to update a sub-partition set's configuration.
 * This is called by run_maintainance() and at least provides a consistent way to check that I know will run. 
 * If anyone can get a working constraint/trigger, please help!
*/

    WITH parent_info AS (
        SELECT c1.oid
        FROM pg_catalog.pg_class c1 
        JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
        WHERE n1.nspname = split_part(p_parent_table, '.', 1)::name
        AND c1.relname = split_part(p_parent_table, '.', 2)::name
    )
    , child_tables AS (
        SELECT n.nspname||'.'||c.relname AS tablename
        FROM pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        JOIN parent_info pi ON h.inhparent = pi.oid
    )
    -- Column order here must match the RETURNS TABLE definition
    -- This column list must be kept consistent between: 
    --   create_parent, check_subpart_sameconfig, create_partition_id, create_partition_time, dump_partitioned_table_definition, and table definition
    SELECT DISTINCT a.sub_partition_type
        , a.sub_control
        , a.sub_partition_interval
        , a.sub_constraint_cols
        , a.sub_premake
        , a.sub_optimize_trigger
        , a.sub_optimize_constraint
        , a.sub_epoch
        , a.sub_inherit_fk
        , a.sub_retention
        , a.sub_retention_schema
        , a.sub_retention_keep_table
        , a.sub_retention_keep_index
        , a.sub_infinite_time_partitions
        , a.sub_automatic_maintenance
        , a.sub_jobmon
        , a.sub_trigger_exception_handling
        , a.sub_upsert
        , a.sub_trigger_return_null
        , a.sub_template_table
        , a.sub_inherit_privileges
        , a.sub_constraint_valid
        , a.sub_subscription_refresh
    FROM @extschema@.part_config_sub a
    JOIN child_tables b on a.sub_parent = b.tablename;
$$;


CREATE OR REPLACE FUNCTION @extschema@.create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_all                   text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze               boolean := FALSE;
v_control               text;
v_control_type          text;
v_exists                text;
v_grantees              text[];
v_hasoids               boolean;
v_id                    bigint;
v_inherit_fk            boolean;
v_inherit_privileges    boolean;
v_job_id                bigint;
v_jobmon                boolean;
v_jobmon_schema         text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_grant          record;
v_parent_schema         text;
v_parent_tablename      text;
v_parent_tablespace     text;
v_partition_interval    bigint;
v_partition_created     boolean := false;
v_partition_name        text;
v_partition_type        text;
v_publications          text[];
v_revoke                text;
v_row                   record;
v_sql                   text;
v_step_id               bigint;
v_sub_control           text;
v_sub_partition_type    text; 
v_sub_id_max            bigint;
v_sub_id_min            bigint;
v_template_table        text;
v_unlogged              char;

BEGIN
/*
 * Function to create id partitions
 */

SELECT control
    , partition_type
    , partition_interval
    , inherit_fk
    , jobmon
    , template_table
    , publications
    , inherit_privileges
INTO v_control
    , v_partition_type
    , v_partition_interval
    , v_inherit_fk
    , v_jobmon
    , v_template_table
    , v_publications
    , v_inherit_privileges
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT n.nspname, c.relname, t.spcname 
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name

AND c.relname = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'id' THEN
    RAISE EXCEPTION 'ERROR: Given parent table is not set up for id/serial partitioning';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::bigint, sub_max::bigint INTO v_sub_id_min, v_sub_id_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'id');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

FOREACH v_id IN ARRAY p_partition_ids LOOP
-- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_id_min IS NOT NULL THEN
        IF v_id < v_sub_id_min OR v_id > v_sub_id_max THEN
            CONTINUE;
        END IF;
    END IF;

    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_id::text, TRUE);
    -- If child table already exists, skip creation
    -- Have to check pg_class because if subpartitioned, table will not be in pg_tables
    SELECT c.relname INTO v_exists 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name AND c.relname = v_partition_name::name;
    IF v_exists IS NOT NULL THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Creating new partition '||v_partition_name||' with interval from '||v_id||' to '||(v_id + v_partition_interval)-1);
    END IF;

    v_sql := 'CREATE';

    -- As of PG12, the unlogged/logged status of a native parent table cannot be changed via an ALTER TABLE in order to affect its children.
    -- As of v4.2x, the unlogged state will be managed via the template table    
    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    IF v_unlogged = 'u' and v_partition_type != 'native'  THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;

    -- Close parentheses on LIKE are below due to differing requirements of native subpartitioning
    -- Same INCLUDING list is used in create_parent()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS '
            , v_parent_schema
            , v_partition_name
            , v_parent_schema
            , v_parent_tablename);

    IF current_setting('server_version_num')::int >= 120000 THEN
        v_sql := v_sql || ' INCLUDING GENERATED ';
    END IF;

    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- INCLUDING INDEXES isn't necessary for native partitioning. It isn't supported in v10 and 
	    --   for v11+ index inheritance is automatically handled when the partition is attached
        v_sql := v_sql || format(') PARTITION BY RANGE (%I) ', v_sub_control);
    ELSE
        v_sql := v_sql || format(' INCLUDING INDEXES) ', v_sub_control);
    END IF;


    IF current_setting('server_version_num')::int < 120000 THEN
        -- column removed from pgclass in pg12
        SELECT relhasoids INTO v_hasoids 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename::name
        AND n.nspname = v_parent_schema::name;
        IF v_hasoids IS TRUE THEN
            v_sql := v_sql || ' WITH (OIDS)';
        END IF;
    END IF;

    IF p_debug THEN
        RAISE NOTICE 'create_partition_id v_sql: %', v_sql;
    END IF;
    EXECUTE v_sql;

    IF v_partition_type = 'native' THEN

        IF current_setting('server_version_num')::int >= 120000 THEN
            -- PG12 fixed tablespace marking on the parent of a native partition set
            -- Versions older than 12 handle tablespace setting via inherit_template_properties() call below
            IF v_parent_tablespace IS NOT NULL THEN
                EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
            END IF;
        END IF;

        IF v_template_table IS NOT NULL THEN
            PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_partition_name);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
            , v_parent_schema
            , v_parent_tablename
            , v_parent_schema
            , v_partition_name
            , v_id
            , v_id + v_partition_interval);

    ELSE -- non-native

        IF v_parent_tablespace IS NOT NULL THEN
            EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %s AND %I < %s )'
            , v_parent_schema
            , v_partition_name
            , v_partition_name||'_partition_check'
            , v_control
            , v_id
            , v_control
            , v_id + v_partition_interval);

        EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I', v_parent_schema, v_partition_name, v_parent_schema, v_parent_tablename);

        -- Indexes cannot be created on the parent, so clustering cannot be used for native yet.
        PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

        -- Foreign keys to other tables not supported on native parent tables
        IF v_inherit_fk THEN
            PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
        END IF;

    END IF;
    
    -- NOTE: Privileges not automatically inherited for native. Only do so if config flag is set
    IF v_partition_type != 'native' OR (v_partition_type = 'native' AND v_inherit_privileges = TRUE) THEN
        PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    -- This column list must be kept consistent between: 
    --   create_parent, check_subpart_sameconfig, create_partition_id, create_partition_time, dump_partitioned_table_definition, and table definition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_infinite_time_partitions
            , sub_automatic_maintenance
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_upsert
            , sub_trigger_return_null
            , sub_template_table
            , sub_inherit_privileges
            , sub_constraint_valid
            , sub_subscription_refresh
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, 'Subpartitioning '||v_partition_name);
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_template_table := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_automatic_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_template_table
            , v_row.sub_jobmon);
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
            , trigger_exception_handling = v_row.sub_trigger_exception_handling
            , upsert = v_row.sub_upsert
            , inherit_privileges = v_row.sub_inherit_privileges
            , trigger_return_null = v_row.sub_trigger_return_null
            , constraint_valid = v_row.sub_constraint_valid
            , subscription_refresh = v_row.sub_subscription_refresh
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

    END LOOP; -- end sub partitioning LOOP
    
    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    IF v_publications IS NOT NULL THEN
        -- NOTE: Publications currently not supported on parent table, but are supported on the table partitions if individually assigned.
        PERFORM @extschema@.apply_publications(p_parent_table, v_parent_schema, v_partition_name);
    END IF;

    v_partition_created := true;

END LOOP;

-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s', p_parent_table));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    PERFORM close_job(v_job_id);
END IF;
 
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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



CREATE OR REPLACE FUNCTION @extschema@.create_partition_time(p_parent_table text, p_partition_times timestamptz[], p_analyze boolean DEFAULT true, p_debug boolean DEFAULT false) 
RETURNS boolean
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_all                           text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_analyze                       boolean := FALSE;
v_control                       text;
v_control_type                  text;
v_datetime_string               text;
v_epoch                         text;
v_exists                        smallint;
v_grantees                      text[];
v_hasoids                       boolean;
v_inherit_privileges            boolean;
v_inherit_fk                    boolean;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_parent_grant                  record;
v_parent_schema                 text;
v_parent_tablename              text;
v_part_col                      text;
v_partition_created             boolean := false;
v_partition_name                text;
v_partition_suffix              text;
v_parent_tablespace             text;
v_partition_expression          text;
v_partition_interval            interval;
v_partition_timestamp_end       timestamptz;
v_partition_timestamp_start     timestamptz;
v_publications                  text[];
v_quarter                       text;
v_revoke                        text;
v_row                           record;
v_sql                           text;
v_step_id                       bigint;
v_step_overflow_id              bigint;
v_sub_control                   text;
v_sub_parent                    text;
v_sub_partition_type            text;
v_sub_timestamp_max             timestamptz;
v_sub_timestamp_min             timestamptz;
v_template_table                text;
v_trunc_value                   text;
v_time                          timestamptz;
v_partition_type                          text;
v_unlogged                      char;
v_year                          text;

BEGIN
/*
 * Function to create a child table in a time-based partition set
 */

SELECT partition_type
    , control
    , partition_interval
    , epoch
    , inherit_fk
    , jobmon
    , datetime_string
    , template_table
    , publications
    , inherit_privileges
INTO v_partition_type
    , v_control
    , v_partition_interval
    , v_epoch
    , v_inherit_fk
    , v_jobmon
    , v_datetime_string
    , v_template_table
    , v_publications
    , v_inherit_privileges
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;

IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: no config found for %', p_parent_table;
END IF;

SELECT n.nspname, c.relname, t.spcname 
INTO v_parent_schema, v_parent_tablename, v_parent_tablespace 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'time' THEN 
    IF (v_control_type = 'id' AND v_epoch = 'none') OR v_control_type <> 'id' THEN
        RAISE EXCEPTION 'Cannot run on partition set without time based control column or epoch flag set with an id column. Found control: %, epoch: %', v_control_type, v_epoch;
    END IF;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Determine if this table is a child of a subpartition parent. If so, get limits of what child tables can be created based on parent suffix
SELECT sub_min::timestamptz, sub_max::timestamptz INTO v_sub_timestamp_min, v_sub_timestamp_max FROM @extschema@.check_subpartition_limits(p_parent_table, 'time');

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN CREATE TABLE: %s', p_parent_table));
END IF;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;
IF p_debug THEN
    RAISE NOTICE 'create_partition_time: v_partition_expression: %', v_partition_expression;
END IF;

FOREACH v_time IN ARRAY p_partition_times LOOP    
    v_partition_timestamp_start := v_time;
    BEGIN
        v_partition_timestamp_end := v_time + v_partition_interval;
    EXCEPTION WHEN datetime_field_overflow THEN
        RAISE WARNING 'Attempted partition time interval is outside PostgreSQL''s supported time range. 
            Child partition creation after time % skipped', v_time;
        v_step_overflow_id := add_step(v_job_id, 'Attempted partition time interval is outside PostgreSQL''s supported time range.');
        PERFORM update_step(v_step_overflow_id, 'CRITICAL', 'Child partition creation after time '||v_time||' skipped');

        CONTINUE;
    END;

    -- Do not create the child table if it's outside the bounds of the top parent. 
    IF v_sub_timestamp_min IS NOT NULL THEN
        IF v_time < v_sub_timestamp_min OR v_time > v_sub_timestamp_max THEN
            CONTINUE;
        END IF;
    END IF;

    -- This suffix generation code is in partition_data_time() as well
    v_partition_suffix := to_char(v_time, v_datetime_string);
    v_partition_name := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);
    -- Check if child exists. 
    SELECT count(*) INTO v_exists
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_parent_schema::name 
    AND c.relname = v_partition_name::name;

    IF v_exists > 0 THEN
        CONTINUE;
    END IF;

    -- Ensure analyze is run if a new partition is created. Otherwise if one isn't, will be false and analyze will be skipped
    v_analyze := TRUE;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Creating new partition %s.%s with interval from %s to %s'
                                                , v_parent_schema
                                                , v_partition_name
                                                , v_partition_timestamp_start
                                                , v_partition_timestamp_end-'1sec'::interval));
    END IF;

    v_sql := 'CREATE';

    -- As of PG12, the unlogged/logged status of a native parent table cannot be changed via an ALTER TABLE in order to affect its children.
    -- As of v4.2x, the unlogged state will be managed via the template table    
    SELECT relpersistence INTO v_unlogged 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_parent_tablename::name
    AND n.nspname = v_parent_schema::name;
    IF v_unlogged = 'u' and v_partition_type != 'native'  THEN
        v_sql := v_sql || ' UNLOGGED';
    END IF;

    -- Close parentheses on LIKE are below due to differing requirements of native subpartitioning
    -- Same INCLUDING list is used in create_parent()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS '
                                , v_parent_schema
                                , v_partition_name
                                , v_parent_schema
                                , v_parent_tablename);

    IF current_setting('server_version_num')::int >= 120000 THEN
        v_sql := v_sql || ' INCLUDING GENERATED ';
    END IF;


    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- INCLUDING INDEXES isn't necessary for native partitioning. It isn't supported in v10 and 
	--   for v11+ index inheritance is automatically handled when the partition is attached
        v_sql := v_sql || format(') PARTITION BY RANGE (%I) ', v_sub_control);
    ELSE
        v_sql := v_sql || format(' INCLUDING INDEXES) ', v_sub_control);
    END IF;

    IF current_setting('server_version_num')::int < 120000 THEN
        -- column removed from pgclass in pg12
        SELECT relhasoids INTO v_hasoids 
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = v_parent_tablename::name
        AND n.nspname = v_parent_schema::name;
        IF v_hasoids IS TRUE THEN
            v_sql := v_sql || ' WITH (OIDS)';
        END IF;
    END IF;

    IF p_debug THEN
        RAISE NOTICE 'create_partition_time v_sql: %', v_sql;
    END IF;
    EXECUTE v_sql;


    IF v_partition_type = 'native' THEN

        IF current_setting('server_version_num')::int >= 120000 THEN
            -- PG12 fixed tablespace marking on the parent of a native partition set
            -- Versions older than 12 handle tablespace setting via inherit_template_properties() call below
            IF v_parent_tablespace IS NOT NULL THEN
                EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
            END IF;
        END IF;

        IF v_template_table IS NOT NULL THEN
            PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_partition_name);
        END IF;

        IF v_epoch = 'none' THEN
            -- Attach with normal, time-based values for native constraint
            EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                , v_parent_schema
                , v_parent_tablename
                , v_parent_schema
                , v_partition_name
                , v_partition_timestamp_start
                , v_partition_timestamp_end);
        ELSE
            -- Must attach with integer based values for native constraint and epoch
            IF v_epoch = 'seconds' THEN
                EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_parent_schema
                    , v_partition_name
                    , EXTRACT('epoch' FROM v_partition_timestamp_start)
                    , EXTRACT('epoch' FROM v_partition_timestamp_end));
            ELSIF v_epoch = 'milliseconds' THEN
                EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_parent_schema
                    , v_partition_name
                    , EXTRACT('epoch' FROM v_partition_timestamp_start) * 1000
                    , EXTRACT('epoch' FROM v_partition_timestamp_end) * 1000);
            END IF;
            -- Create secondary, time-based constraint since native's constraint is already integer based
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%s >= %L AND %4$s < %6$L)'
                , v_parent_schema
                , v_partition_name
                , v_partition_name||'_partition_check'
                , v_partition_expression
                , v_partition_timestamp_start
                , v_partition_timestamp_end);
        END IF;
    ELSE -- non-native

        IF v_parent_tablespace IS NOT NULL THEN
            EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
        END IF;

        -- Non-native always gets time-based constraint
        EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%s >= %L AND %4$s < %6$L)'
            , v_parent_schema
            , v_partition_name
            , v_partition_name||'_partition_check'
            , v_partition_expression
            , v_partition_timestamp_start
            , v_partition_timestamp_end);
        IF v_epoch = 'seconds' THEN
            -- Non-native needs secondary, integer based constraint for epoch
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                            , v_parent_schema
                            , v_partition_name
                            , v_partition_name||'_partition_int_check'
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_start)
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end) );
        ELSIF v_epoch = 'milliseconds' THEN
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                            , v_parent_schema
                            , v_partition_name
                            , v_partition_name||'_partition_int_check'
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_start) * 1000
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end) * 1000);
        END IF;

        EXECUTE format('ALTER TABLE %I.%I INHERIT %I.%I'
                        , v_parent_schema
                        , v_partition_name
                        , v_parent_schema
                        , v_parent_tablename);

        -- If custom time, set extra config options.
        IF v_partition_type = 'time-custom' THEN
            INSERT INTO @extschema@.custom_time_partitions (parent_table, child_table, partition_range)
            VALUES ( p_parent_table, v_parent_schema||'.'||v_partition_name, tstzrange(v_partition_timestamp_start, v_partition_timestamp_end, '[)') );
        END IF;

        -- Indexes cannot be created on the parent, so clustering cannot be used for native yet.
        PERFORM @extschema@.apply_cluster(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name);

        -- Foreign keys to other tables not supported in native
        IF v_inherit_fk THEN
            PERFORM @extschema@.apply_foreign_keys(p_parent_table, v_parent_schema||'.'||v_partition_name, v_job_id);
        END IF;

    END IF; -- end native check

    -- NOTE: Privileges not automatically inherited for native. Only do so if config flag is set
    IF v_partition_type != 'native' OR (v_partition_type = 'native' AND v_inherit_privileges = TRUE) THEN
        PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_parent_schema, v_partition_name, v_job_id);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    -- Will only loop once and only if sub_partitioning is actually configured
    -- This seemed easier than assigning a bunch of variables then doing an IF condition
    -- This column list must be kept consistent between: 
    --   create_parent, check_subpart_sameconfig, create_partition_id, create_partition_time, dump_partitioned_table_definition, and table definition
    FOR v_row IN 
        SELECT sub_parent
            , sub_partition_type
            , sub_control
            , sub_partition_interval
            , sub_constraint_cols
            , sub_premake
            , sub_optimize_trigger
            , sub_optimize_constraint
            , sub_epoch
            , sub_inherit_fk
            , sub_retention
            , sub_retention_schema
            , sub_retention_keep_table
            , sub_retention_keep_index
            , sub_infinite_time_partitions
            , sub_automatic_maintenance
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_upsert
            , sub_trigger_return_null
            , sub_template_table
            , sub_inherit_privileges
            , sub_constraint_valid
            , sub_subscription_refresh
        FROM @extschema@.part_config_sub
        WHERE sub_parent = p_parent_table
    LOOP
        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Subpartitioning %s.%s', v_parent_schema, v_partition_name));
        END IF;
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_template_table := %L
                , p_jobmon := %L )'
            , v_parent_schema||'.'||v_partition_name
            , v_row.sub_control
            , v_row.sub_partition_type
            , v_row.sub_partition_interval
            , v_row.sub_constraint_cols
            , v_row.sub_premake
            , v_row.sub_automatic_maintenance
            , v_row.sub_inherit_fk
            , v_row.sub_epoch
            , v_row.sub_template_table
            , v_row.sub_jobmon);
        IF p_debug THEN
            RAISE NOTICE 'create_partition_time (create_parent loop): %', v_sql;
        END IF;
        EXECUTE v_sql;

        UPDATE @extschema@.part_config SET 
            retention_schema = v_row.sub_retention_schema
            , retention_keep_table = v_row.sub_retention_keep_table
            , retention_keep_index = v_row.sub_retention_keep_index
            , optimize_trigger = v_row.sub_optimize_trigger
            , optimize_constraint = v_row.sub_optimize_constraint
            , infinite_time_partitions = v_row.sub_infinite_time_partitions
            , trigger_exception_handling = v_row.sub_trigger_exception_handling
            , upsert = v_row.sub_upsert
            , inherit_privileges = v_row.sub_inherit_privileges
            , trigger_return_null = v_row.sub_trigger_return_null
            , constraint_valid = v_row.sub_constraint_valid
            , subscription_refresh = v_row.sub_subscription_refresh
        WHERE parent_table = v_parent_schema||'.'||v_partition_name;

    END LOOP; -- end sub partitioning LOOP

    -- Manage additonal constraints if set
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id, p_debug := p_debug);

    IF v_publications IS NOT NULL THEN
        -- NOTE: Publications currently not supported on parent table, but are supported on the table partitions if individually assigned.
        PERFORM @extschema@.apply_publications(p_parent_table, v_parent_schema, v_partition_name);
    END IF;

    v_partition_created := true;

END LOOP;
-- v_analyze is a local check if a new table is made.
-- p_analyze is a parameter to say whether to run the analyze at all. Used by create_parent() to avoid long exclusive lock or run_maintenence() to avoid long creation runs.
IF v_analyze AND p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Analyzing partition set: %s', p_parent_table));
    END IF;

    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF v_partition_created = false THEN
        v_step_id := add_step(v_job_id, format('No partitions created for partition set: %s. Attempted intervals: %s', p_parent_table, p_partition_times));
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;

    IF v_step_overflow_id IS NOT NULL THEN
        PERFORM fail_job(v_job_id);
    ELSE
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_partition_created;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN CREATE TABLE: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


CREATE OR REPLACE FUNCTION @extschema@.dump_partitioned_table_definition(
  p_parent_table TEXT,
  p_ignore_template_table BOOLEAN DEFAULT false
) RETURNS TEXT
  LANGUAGE PLPGSQL STABLE
AS $$
DECLARE
  v_create_parent_definition TEXT;
  v_update_part_config_definition TEXT;
  -- Columns from part_config table.
  v_parent_table TEXT; -- NOT NULL
  v_control TEXT; -- NOT NULL
  v_partition_type TEXT; -- NOT NULL
  v_partition_interval TEXT; -- NOT NULL
  v_constraint_cols TEXT[];
  v_premake integer; -- NOT NULL
  v_optimize_trigger integer; -- NOT NULL
  v_optimize_constraint integer; -- NOT NULL
  v_epoch text; -- NOT NULL
  v_inherit_fk BOOLEAN; -- NOT NULL
  v_retention TEXT;
  v_retention_schema TEXT;
  v_retention_keep_table BOOLEAN; -- NOT NULL
  v_retention_keep_index BOOLEAN; -- NOT NULL
  v_infinite_time_partitions BOOLEAN; -- NOT NULL
  v_datetime_string TEXT;
  v_automatic_maintenance TEXT; -- NOT NULL
  v_jobmon BOOLEAN; -- NOT NULL
  v_sub_partition_set_full BOOLEAN; -- NOT NULL
  v_trigger_exception_handling BOOLEAN;
  v_upsert TEXT; -- NOT NULL
  v_trigger_return_null BOOLEAN; -- NOT NULL
  v_template_table TEXT;
  v_publications TEXT[];
  v_inherit_privileges BOOLEAN; -- DEFAULT false
  v_constraint_valid BOOLEAN; -- DEFAULT true NOT NULL
  v_subscription_refresh text;
BEGIN
  SELECT
    pc.parent_table,
    pc.control,
    pc.partition_type,
    pc.partition_interval,
    pc.constraint_cols,
    pc.premake,
    pc.optimize_trigger,
    pc.optimize_constraint,
    pc.epoch,
    pc.inherit_fk,
    pc.retention,
    pc.retention_schema,
    pc.retention_keep_table,
    pc.retention_keep_index,
    pc.infinite_time_partitions,
    pc.datetime_string,
    pc.automatic_maintenance,
    pc.jobmon,
    pc.sub_partition_set_full,
    pc.trigger_exception_handling,
    pc.upsert,
    pc.trigger_return_null,
    pc.template_table,
    pc.publications,
    pc.inherit_privileges,
    pc.constraint_valid, 
    pc.subscription_refresh
  INTO
    v_parent_table,
    v_control,
    v_partition_type,
    v_partition_interval,
    v_constraint_cols,
    v_premake,
    v_optimize_trigger,
    v_optimize_constraint,
    v_epoch,
    v_inherit_fk,
    v_retention,
    v_retention_schema,
    v_retention_keep_table,
    v_retention_keep_index,
    v_infinite_time_partitions,
    v_datetime_string,
    v_automatic_maintenance,
    v_jobmon,
    v_sub_partition_set_full,
    v_trigger_exception_handling,
    v_upsert,
    v_trigger_return_null,
    v_template_table,
    v_publications,
    v_inherit_privileges,
    v_constraint_valid,
    v_subscription_refresh
  FROM @extschema@.part_config pc
  WHERE pc.parent_table = p_parent_table;

  IF v_partition_type = 'partman' THEN
    CASE
      WHEN v_partition_interval::INTERVAL = '1 year'::INTERVAL THEN
        v_partition_interval := 'yearly';
      WHEN v_partition_interval::INTERVAL = '3 months'::INTERVAL THEN
        v_partition_interval := 'quarterly';
      WHEN v_partition_interval::INTERVAL = '1 month'::INTERVAL THEN
        v_partition_interval := 'monthly';
      WHEN v_partition_interval::INTERVAL = '1 week'::INTERVAL THEN
        v_partition_interval := 'weekly';
      WHEN v_partition_interval::INTERVAL = '1 day'::INTERVAL THEN
        v_partition_interval := 'daily';
      WHEN v_partition_interval::INTERVAL = '1 hour'::INTERVAL THEN
        v_partition_interval := 'hourly';
      WHEN v_partition_interval::INTERVAL = '30 mins'::INTERVAL THEN
        v_partition_interval := 'half-hour';
      WHEN v_partition_interval::INTERVAL = '15 mins'::INTERVAL THEN
        v_partition_interval := 'quarter-hour';
      ELSE
        RAISE EXCEPTION 'Partitioning interval not recognized for "partman" partitioning type';
    END CASE;
  END IF;

  IF v_partition_type = 'native' AND p_ignore_template_table THEN
    v_template_table := NULL;
  END IF;

  v_create_parent_definition := format(
E'SELECT @extschema@.create_parent(
\tp_parent_table := %L,
\tp_control := %L,
\tp_type := %L,
\tp_interval := %L,
\tp_constraint_cols := %L,
\tp_premake := %s,
\tp_automatic_maintenance := %L,
\tp_inherit_fk := %L,
\tp_epoch := %L,
\tp_upsert := %L,
\tp_publications := %L,
\tp_trigger_return_null := %L,
\tp_template_table := %L,
\tp_jobmon := %L
\t-- v_start_partition is intentionally ignored as there
\t-- isn''t any obviously correct definition.
);',
      v_parent_table,
      v_control,
      v_partition_type,
      v_partition_interval,
      v_constraint_cols,
      v_premake,
      v_automatic_maintenance,
      v_inherit_fk,
      v_epoch,
      v_upsert,
      v_publications,
      v_trigger_return_null,
      v_template_table,
      v_jobmon
    );

  v_update_part_config_definition := format(
E'UPDATE @extschema@.part_config SET
\toptimize_trigger = %s,
\toptimize_constraint = %s,
\tretention = %L,
\tretention_schema = %L,
\tretention_keep_table = %L,
\tretention_keep_index = %L,
\tinfinite_time_partitions = %L,
\tdatetime_string = %L,
\tsub_partition_set_full = %L,
\ttrigger_exception_handling = %L,
\tinherit_privileges = %L,
\tconstraint_valid = %L,
\tsubscription_refresh = %L
WHERE parent_table = %L;',
    v_optimize_trigger,
    v_optimize_constraint,
    v_retention,
    v_retention_schema,
    v_retention_keep_table,
    v_retention_keep_index,
    v_infinite_time_partitions,
    v_datetime_string,
    v_sub_partition_set_full,
    v_trigger_exception_handling,
    v_inherit_privileges,
    v_constraint_valid,
    v_subscription_refresh,
    v_parent_table
  );

  RETURN concat_ws(E'\n',
    v_create_parent_definition,
    v_update_part_config_definition
  );
END
$$;


CREATE OR REPLACE FUNCTION @extschema@.apply_privileges(p_parent_schema text, p_parent_tablename text, p_child_schema text, p_child_tablename text, p_job_id bigint DEFAULT NULL) RETURNS void
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_all               text[] := ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'];
v_child_grant       record;
v_child_owner       text;
v_grantees          text[];
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_match             boolean;
v_parent_grant      record;
v_parent_owner      text;
v_revoke            text;
v_row_revoke        record;
v_sql               text;
v_step_id           bigint;

BEGIN
/*
 * Apply privileges and ownership that exist on a given parent to the given child table
 */

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_schema ||'.'|| p_parent_tablename;
IF v_jobmon IS NULL THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %.%', p_parent_schema, p_parent_tablename;
END IF;

SELECT pg_get_userbyid(c.relowner) INTO v_parent_owner 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_parent_schema::name 
AND c.relname = p_parent_tablename::name;

SELECT pg_get_userbyid(c.relowner) INTO v_child_owner
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_child_schema::name
AND c.relname = p_child_tablename::name;

IF v_parent_owner IS NULL THEN
    RAISE EXCEPTION 'Given parent table does not exist: %.%', p_parent_schema, p_parent_tablename;
END IF;
IF v_child_owner IS NULL THEN
    RAISE EXCEPTION 'Given child table does not exist: %.%', p_child_schema, p_child_tablename;
END IF;

IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_namespace n, pg_extension e WHERE e.extname = 'pg_jobmon' AND e.extnamespace = n.oid;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        EXECUTE format('SELECT %I.add_job(%L)', v_jobmon_schema, format('PARTMAN APPLYING PRIVILEGES TO CHILD TABLE: %s.%s', p_child_schema, p_child_tablename)) INTO v_job_id;
    ELSE
        v_job_id := p_job_id;
    END IF;
    EXECUTE format('SELECT %I.add_step(%L, %L)', v_jobmon_schema, v_job_id, format('Setting new child table privileges for %s.%s', p_child_schema, p_child_tablename)) INTO v_step_id;
END IF;

IF v_jobmon_schema IS NOT NULL THEN

    EXECUTE format('SELECT %I.update_step(%L, %L, %L)'
            , v_jobmon_schema
            , v_step_id
            , 'PENDING'
            , format('Applying privileges on child partition: %s.%s'
                , p_child_schema
                , p_child_tablename)
            );
END IF;

FOR v_parent_grant IN 
    SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
            , grantee
    FROM @extschema@.table_privs
    WHERE table_schema = p_parent_schema::name AND table_name = p_parent_tablename::name
    GROUP BY grantee 
LOOP
    -- Compare parent & child grants. Don't re-apply if it already exists
    v_match := false;
    v_sql := NULL;
    FOR v_child_grant IN 
        SELECT array_agg(DISTINCT privilege_type::text ORDER BY privilege_type::text) AS types
                , grantee
        FROM @extschema@.table_privs 
        WHERE table_schema = p_child_schema::name AND table_name = p_child_tablename::name
        GROUP BY grantee 
    LOOP
        IF v_parent_grant.types = v_child_grant.types AND v_parent_grant.grantee = v_child_grant.grantee THEN
            v_match := true;
        END IF;
    END LOOP;

    IF v_match = false THEN
        IF v_parent_grant.grantee = 'PUBLIC' THEN
            v_sql := 'GRANT %s ON %I.%I TO %s';
        ELSE
            v_sql := 'GRANT %s ON %I.%I TO %I';
        END IF;
        EXECUTE format(v_sql
                        , array_to_string(v_parent_grant.types, ',')
                        , p_child_schema
                        , p_child_tablename
                        , v_parent_grant.grantee);
        v_sql := NULL;
        SELECT string_agg(r, ',') INTO v_revoke FROM (SELECT unnest(v_all) AS r EXCEPT SELECT unnest(v_parent_grant.types)) x;
        IF v_revoke IS NOT NULL THEN
            IF v_parent_grant.grantee = 'PUBLIC' THEN
                v_sql := 'REVOKE %s ON %I.%I FROM %s CASCADE';
            ELSE
                v_sql := 'REVOKE %s ON %I.%I FROM %I CASCADE';
            END IF;
            EXECUTE format(v_sql
                        , v_revoke
                        , p_child_schema
                        , p_child_tablename
                        , v_parent_grant.grantee);
            v_sql := NULL;
        END IF;
    END IF;

    v_grantees := array_append(v_grantees, v_parent_grant.grantee::text);

END LOOP;

-- Revoke all privileges from roles that have none on the parent
IF v_grantees IS NOT NULL THEN
    FOR v_row_revoke IN 
        SELECT role FROM (
            SELECT DISTINCT grantee::text AS role FROM @extschema@.table_privs WHERE table_schema = p_child_schema::name AND table_name = p_child_tablename::name
            EXCEPT
            SELECT unnest(v_grantees)) x
    LOOP
        IF v_row_revoke.role IS NOT NULL THEN
            IF v_row_revoke.role = 'PUBLIC' THEN
                v_sql := 'REVOKE ALL ON %I.%I FROM %s';
            ELSE
                v_sql := 'REVOKE ALL ON %I.%I FROM %I';
            END IF;
            EXECUTE format(v_sql
                        , p_child_schema
                        , p_child_tablename
                        , v_row_revoke.role);
        END IF;
    END LOOP;

END IF;

IF v_parent_owner <> v_child_owner THEN
    EXECUTE format('ALTER TABLE %I.%I OWNER TO %I'
                , p_child_schema
                , p_child_tablename
                , v_parent_owner);
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    EXECUTE format('SELECT %I.update_step(%L, %L, %L)', v_jobmon_schema, v_step_id, 'OK', 'Done');
    IF p_job_id IS NULL THEN
        EXECUTE format('SELECT %I.close_job(%L)', v_jobmon_schema, v_job_id);
    END IF;
END IF;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s'')', v_jobmon_schema, p_parent_tablename) INTO v_job_id;
                EXECUTE format('SELECT %I.add_step(%s, ''EXCEPTION before job logging started'')', v_jobmon_schema, v_job_id, p_parent_tablename) INTO v_step_id;
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


CREATE OR REPLACE FUNCTION @extschema@.check_automatic_maintenance_value (p_automatic_maintenance text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_automatic_maintenance IN ('on', 'off') INTO v_result;
    RETURN v_result;
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

DROP TABLE IF EXISTS partman_preserve_privs_temp;
