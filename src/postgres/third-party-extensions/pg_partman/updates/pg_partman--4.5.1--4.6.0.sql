-- Support for PostgreSQL 14

-- Better handle custom intervals that don't fall on typical datetime boundaries (monthly, daily, etc). Previously if you chose a custom interval that did not initially align on a typical boundary, pg_partman would always try and truncate the initial table's datetime start value to one of those typical boundaries. This could lead to child table constraints that were not expected. A new parameter to create_parent(), 'p_date_trunc_interval' has been added to allow you to tell pg_partman how to truncate the initial datetime value to better meet custom interval needs. This accepts any valid interval value that the built-in 'date_trunc()' function would accept. (Ex: 9 week interval starting on 2021-04-05 would need to be truncated by 'week' vs 'month'. Otherwise pg_partman would truncate by 'month' and the partitions would start on 2021-04-01 and result in unexpected child table boundaries).

-- When dropping tables for retention, DROP...CASCADE is no longer the default. This avoids foreign key references TO the partition set getting dropped if any child table is dropped. Set the option "drop_cascade_fk" in part_config to allow the old behavior if desired. This cascade is only allowed with non-native, trigger-based partitioning and is not supported with sub-partitioning at this time.

-- Fixed show_partition_info() to properly return boundary values no matter what custom interval may be used for the partition set. Update documentation to note that the show_partition_info() boundary results are INCLUSIVE for returned start values and EXCLUSIVE for returned end values.

-- Added p_drop_cascade option to undo_partition()/undo_partition_proc() to allow undoing sub-partition sets from parent tables higher in the inheritance tree. Only applies when p_keep_tables is set to false. Note this causes all child tables below a sub-partition parent to be dropped when that parent is dropped.

-- Deprecate pg_partman's specialized UPSERT support for native partitioning in PG11+. Raise error for users to use built-in INSERT ON CONFLICT feature instead.

-- Better support NOTIFY commands with the extension's built-in background worker. Thank you to @eisaev on Github for this fix.  (GitHub Issue #384).

-- Fixed edge case in sub-partitioning where an additional subpartition child table could be created that is just outside the max boundary limit of its parent table boundaries.

-- Added new optional parameter to drop_partition_time() to tell partman to use a different reference timestamp from which to determine which partitions should be affected. Thanks to @wesselvdv on Github for this feature (Github PR#386).

ALTER TABLE @extschema@.part_config ADD COLUMN drop_cascade_fk BOOLEAN NOT NULL DEFAULT false;
ALTER TABLE @extschema@.part_config_sub ADD COLUMN sub_date_trunc_interval TEXT;

CREATE TEMP TABLE partman_preserve_privs_temp (statement text);

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.check_subpart_sameconfig(text)  TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'check_subpart_sameconfig'
AND grantee != 'PUBLIC';

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_parent(text, text, text, text, text[], int, text, text, boolean, text, text, text[], boolean, text, boolean, text) TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_parent'
AND grantee != 'PUBLIC';

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.create_sub_parent(text, text, text, text, text, text[], int, text, boolean, text, text,  boolean, boolean , text) TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'create_sub_parent'
AND grantee != 'PUBLIC';

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.undo_partition(text, int, text, boolean, numeric, text, text[], boolean) TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition'
AND grantee != 'PUBLIC';

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON FUNCTION @extschema@.drop_partition_time(text, interval, boolean, boolean, text, timestamptz) TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'drop_partition_time'
AND grantee != 'PUBLIC';

DROP FUNCTION @extschema@.check_subpart_sameconfig(text);
DROP FUNCTION @extschema@.create_parent(text, text, text, text, text[], int, text, text, boolean, text, text, text[], boolean, text, boolean);
DROP FUNCTION @extschema@.create_sub_parent(text, text, text, text, text, text[], int, text, boolean, text, text, boolean, boolean);
DROP FUNCTION @extschema@.undo_partition(text, int, text, boolean, numeric, text, text[]);
DROP FUNCTION @extschema@.drop_partition_time(text, interval, boolean, boolean, text);

CREATE OR REPLACE FUNCTION @extschema@.drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_control                   text;
v_control_type              text;
v_count                     int;
v_drop_cascade_fk           boolean;
v_drop_count                int := 0;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_max                       bigint;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        bigint;
v_partition_id              bigint;
v_partition_type            text;
v_retention                 bigint;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_row_max_id                record;
v_sql                       text;
v_step_id                   bigint;
v_sub_parent                text;

BEGIN
/*
 * Function to drop child tables from an id-based partition set. 
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_id'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_id already running.';
    RETURN 0;
END IF;

IF p_retention IS NULL THEN
    SELECT  
        partition_interval::bigint
        , partition_type
        , control
        , retention::bigint
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
        , drop_cascade_fk
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
        , v_drop_cascade_fk
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE -- Allow override of configuration options
     SELECT  
        partition_interval::bigint
        , partition_type
        , control
        , retention_keep_table
        , retention_keep_index
        , retention_schema
        , jobmon
        , drop_cascade_fk
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
        , v_drop_cascade_fk
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type <> 'id' THEN
    RAISE EXCEPTION 'Data type of control column in given partition set is not an integer type';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

-- Loop through child tables starting from highest to get current max value in partition set
-- Avoids doing a scan on entire partition set and/or getting any values accidentally in parent.
FOR v_row_max_id IN
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'DESC')
LOOP
        EXECUTE format('SELECT max(%I) FROM %I.%I', v_control, v_row_max_id.partition_schemaname, v_row_max_id.partition_tablename) INTO v_max;
        IF v_max IS NOT NULL THEN
            EXIT;
        END IF;
END LOOP;

SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = p_parent_table;

-- Loop through child tables of the given parent
-- Must go in ascending order to avoid dropping what may be the "last" partition in the set after dropping tables that match retention period
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
     SELECT child_start_id INTO v_partition_id FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);

    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention <= (v_max - (v_partition_id + v_partition_interval)) THEN

        -- Do not allow final partition to be dropped if it is not a sub-partition parent
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 AND v_sub_parent IS NULL THEN
            RAISE WARNING 'Attempt to drop final partition in partition set % as part of retention policy. If you see this message multiple times for the same table, advise reviewing retention policy and/or data entry into the partition set. Also consider setting "infinite_time_partitions = true" if there are large gaps in data insertion.).', p_parent_table;
            CONTINUE;
        END IF;

        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP ID PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Detach/Uninherit table %s.%s from %s', v_row.partition_schemaname, v_row.partition_tablename, p_parent_table));
        END IF;

        IF v_retention_keep_table = true THEN
            -- No need to detach partition before dropping since it's going away anyway
            -- Avoids issue of FKs not allowing detachment (Github Issue #294).
            IF v_partition_type = 'native' THEN
                EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_row.partition_schemaname
                    , v_row.partition_tablename);
            ELSE
                EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_parent_schema
                    , v_parent_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            END IF;
        END IF;

        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                v_sql := 'DROP TABLE %I.%I';
                IF v_drop_cascade_fk OR v_sub_parent IS NOT NULL THEN
                    v_sql := v_sql || ' CASCADE';
                END IF;
                EXECUTE format(v_sql, v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                IF v_partition_type = 'partman' OR 
                       ( v_partition_type = 'native' AND  current_setting('server_version_num')::int < 110000) THEN
                    -- Cannot drop child indexes on native partition sets in PG11+
                    FOR v_index IN 
                         WITH child_info AS (
                            SELECT c1.oid
                            FROM pg_catalog.pg_class c1
                            JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                            WHERE c1.relname = v_row.partition_tablename::name
                            AND n1.nspname = v_row.partition_schema::name
                        )
                        SELECT c.relname as name
                            , con.conname
                        FROM pg_catalog.pg_index i
                        JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                        LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                        JOIN child_info ON i.indrelid = child_info.oid
                    LOOP
                        IF v_jobmon_schema IS NOT NULL THEN
                            v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                                , v_index.name
                                , v_row.partition_schemaname
                                , v_row.partition_tablename));
                        END IF;
                        IF v_index.conname IS NOT NULL THEN
                            EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I', v_row.partition_schemaname, v_row.partition_tablename, v_index.conname);
                        ELSE
                            EXECUTE format('DROP INDEX %I.%I', v_row.partition_schemaname, v_index.name);
                        END IF;
                        IF v_jobmon_schema IS NOT NULL THEN
                            PERFORM update_step(v_step_id, 'OK', 'Done');
                        END IF;
                    END LOOP;
                END IF; -- end native/11 check 
            END IF; -- end v_retention_keep_index IF
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                        , v_row.partition_schemaname
                                                        , v_row.partition_tablename
                                                        , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I'
                    , v_row.partition_schemaname
                    , v_row.partition_tablename
                    , v_retention_schema);

            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname ||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP ID PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


CREATE FUNCTION @extschema@.drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL, p_reference_timestamp timestamptz DEFAULT CURRENT_TIMESTAMP) RETURNS int
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                  text;
ex_detail                   text;
ex_hint                     text;
ex_message                  text;
v_adv_lock                  boolean;
v_control                   text;
v_control_type              text;
v_count                     int;
v_datetime_string           text;
v_drop_cascade_fk           boolean;
v_drop_count                int := 0;
v_epoch                     text;
v_index                     record;
v_job_id                    bigint;
v_jobmon                    boolean;
v_jobmon_schema             text;
v_new_search_path           text := '@extschema@,pg_temp';
v_old_search_path           text;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_interval        interval;
v_partition_timestamp       timestamptz;
v_partition_type            text;
v_retention                 interval;
v_retention_keep_index      boolean;
v_retention_keep_table      boolean;
v_retention_schema          text;
v_row                       record;
v_sql                       text;
v_step_id                   bigint;
v_sub_parent                text;

BEGIN
/*
 * Function to drop child tables from a time-based partition set.
 * Options to move table to different schema, drop only indexes or actually drop the table from the database.
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman drop_partition_time'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'drop_partition_time already running.';
    RETURN 0;
END IF;

-- Allow override of configuration options
IF p_retention IS NULL THEN
    SELECT  
        partition_type
        , control
        , partition_interval::interval
        , epoch
        , retention::interval
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_partition_type
        , v_control
        , v_partition_interval
        , v_epoch
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table 
    AND retention IS NOT NULL;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table with a retention period not found: %', p_parent_table;
    END IF;
ELSE
    SELECT  
        partition_type
        , partition_interval::interval
        , epoch
        , retention_keep_table
        , retention_keep_index
        , datetime_string
        , retention_schema
        , jobmon
    INTO
        v_partition_type
        , v_partition_interval
        , v_epoch
        , v_retention_keep_table
        , v_retention_keep_index
        , v_datetime_string
        , v_retention_schema
        , v_jobmon
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    v_retention := p_retention;

    IF v_partition_interval IS NULL THEN
        RAISE EXCEPTION 'Configuration for given parent table not found: %', p_parent_table;
    END IF;
END IF;

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

IF p_keep_table IS NOT NULL THEN
    v_retention_keep_table = p_keep_table;
END IF;
IF p_keep_index IS NOT NULL THEN
    v_retention_keep_index = p_keep_index;
END IF;
IF p_retention_schema IS NOT NULL THEN
    v_retention_schema = p_retention_schema;
END IF;

SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;

SELECT sub_parent INTO v_sub_parent FROM @extschema@.part_config_sub WHERE sub_parent = p_parent_table;

-- Loop through child tables of the given parent
-- Must go in ascending order to avoid dropping what may be the "last" partition in the set after dropping tables that match retention period
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC')
LOOP
    -- pull out datetime portion of partition's tablename to make the next one
     SELECT child_start_time INTO v_partition_timestamp FROM @extschema@.show_partition_info(v_row.partition_schemaname||'.'||v_row.partition_tablename
        , v_partition_interval::text
        , p_parent_table);
    -- Add one interval since partition names contain the start of the constraint period
    IF v_retention < (p_reference_timestamp - (v_partition_timestamp + v_partition_interval)) THEN

        -- Do not allow final partition to be dropped if it is not a sub-partition parent
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 AND v_sub_parent IS NULL THEN
            RAISE WARNING 'Attempt to drop final partition in partition set % as part of retention policy. If you see this message multiple times for the same table, advise reviewing retention policy and/or data entry into the partition set. Also consider setting "infinite_time_partitions = true" if there are large gaps in data insertion.).', p_parent_table;
            CONTINUE;
        END IF;

        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP TIME PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Detach/Uninherit table %s.%s from %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , p_parent_table));
        END IF;
        IF v_retention_keep_table = true THEN
            -- No need to detach partition before dropping since it's going away anyway
            -- Avoids issue of FKs not allowing detachment (Github Issue #294).
            IF v_partition_type = 'native' THEN
                EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_row.partition_schemaname
                    , v_row.partition_tablename);
            ELSE
                EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                        , v_row.partition_schemaname
                        , v_row.partition_tablename
                        , v_parent_schema
                        , v_parent_tablename);
            END IF;
        END IF;
        IF v_partition_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', 'Done');
        END IF;

        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                v_sql := 'DROP TABLE %I.%I';
                IF v_drop_cascade_fk OR v_sub_parent IS NOT NULL THEN
                    v_sql := v_sql || ' CASCADE';
                END IF;
                EXECUTE format(v_sql, v_row.partition_schemaname, v_row.partition_tablename);
                IF v_jobmon_schema IS NOT NULL THEN
                    PERFORM update_step(v_step_id, 'OK', 'Done');
                END IF;
            ELSIF v_retention_keep_index = false THEN
                IF v_partition_type = 'partman' OR 
                       ( v_partition_type = 'native' AND  current_setting('server_version_num')::int < 110000) THEN
                    -- Cannot drop child indexes on native partition sets in PG11+
                    FOR v_index IN 
                        WITH child_info AS (
                            SELECT c1.oid
                            FROM pg_catalog.pg_class c1
                            JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
                            WHERE c1.relname = v_row.partition_tablename::name
                            AND n1.nspname = v_row.partition_schemaname::name
                        )
                        SELECT c.relname as name
                            , con.conname
                        FROM pg_catalog.pg_index i
                        JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
                        LEFT JOIN pg_catalog.pg_constraint con ON i.indexrelid = con.conindid
                        JOIN child_info ON i.indrelid = child_info.oid
                    LOOP
                        IF v_jobmon_schema IS NOT NULL THEN
                            v_step_id := add_step(v_job_id, format('Drop index %s from %s.%s'
                                                                , v_index.name
                                                                , v_row.partition_schemaname
                                                                , v_row.partition_tablename));
                        END IF;
                        IF v_index.conname IS NOT NULL THEN
                            EXECUTE format('ALTER TABLE %I.%I DROP CONSTRAINT %I'
                                            , v_row.partition_schemaname
                                            , v_row.partition_tablename
                                            , v_index.conname);
                        ELSE
                            EXECUTE format('DROP INDEX %I.%I', v_parent_schema, v_index.name);
                        END IF;
                        IF v_jobmon_schema IS NOT NULL THEN
                            PERFORM update_step(v_step_id, 'OK', 'Done');
                        END IF;
                    END LOOP;
                END IF; -- end native/11 check 
            END IF; -- end v_retention_keep_index IF
        ELSE -- Move to new schema
            IF v_jobmon_schema IS NOT NULL THEN
                v_step_id := add_step(v_job_id, format('Moving table %s.%s to schema %s'
                                                , v_row.partition_schemaname
                                                , v_row.partition_tablename
                                                , v_retention_schema));
            END IF;

            EXECUTE format('ALTER TABLE %I.%I SET SCHEMA %I', v_row.partition_schemaname, v_row.partition_tablename, v_retention_schema);


            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', 'Done');
            END IF;
        END IF; -- End retention schema if

        -- If child table is a subpartition, remove it from part_config & part_config_sub (should cascade due to FK)
        DELETE FROM @extschema@.part_config WHERE parent_table = v_row.partition_schemaname||'.'||v_row.partition_tablename;

        v_drop_count := v_drop_count + 1;
    END IF; -- End retention check IF

END LOOP; -- End child table loop

IF v_jobmon_schema IS NOT NULL THEN
    IF v_job_id IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Finished partition drop maintenance');
        PERFORM update_step(v_step_id, 'OK', format('%s partitions dropped.', v_drop_count));
        PERFORM close_job(v_job_id);
    END IF;
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_drop_count;

EXCEPTION
    WHEN OTHERS THEN
        GET STACKED DIAGNOSTICS ex_message = MESSAGE_TEXT,
                                ex_context = PG_EXCEPTION_CONTEXT,
                                ex_detail = PG_EXCEPTION_DETAIL,
                                ex_hint = PG_EXCEPTION_HINT;
        IF v_jobmon_schema IS NOT NULL THEN
            IF v_job_id IS NULL THEN
                EXECUTE format('SELECT %I.add_job(''PARTMAN DROP TIME PARTITION: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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
  v_drop_cascade_fk boolean; -- DEFAULT false NOT NULL
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
    pc.subscription_refresh,
    pc.drop_cascade_fk
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
    v_subscription_refresh,
    v_drop_cascade_fk
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


CREATE OR REPLACE FUNCTION @extschema@.reapply_privileges(p_parent_table text) RETURNS void
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context          text;
ex_detail           text;
ex_hint             text;
ex_message          text;
v_job_id            bigint;
v_jobmon            boolean;
v_jobmon_schema     text;
v_new_search_path   text := '@extschema@,pg_temp';
v_old_search_path   text;
v_parent_schema     text;
v_parent_tablename  text;
v_row               record;
v_step_id           bigint;

BEGIN

/*
 * Function to re-apply ownership & privileges on all child tables in a partition set using parent table as reference
 */

SELECT jobmon INTO v_jobmon FROM @extschema@.part_config WHERE parent_table = p_parent_table;
IF v_jobmon IS NULL THEN
    RAISE EXCEPTION 'Given table is not managed by this extention: %', p_parent_table;
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');


SELECT schemaname, tablename INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_tables
WHERE schemaname = split_part(p_parent_table, '.', 1)::name
AND tablename = split_part(p_parent_table, '.', 2)::name;
IF v_parent_tablename IS NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'Given parent table does not exist: %', p_parent_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s', p_parent_table));
END IF;

FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table, 'ASC', p_include_default := true)
LOOP
    PERFORM @extschema@.apply_privileges(v_parent_schema, v_parent_tablename, v_row.partition_schemaname, v_row.partition_tablename, v_job_id);
END LOOP;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
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
                EXECUTE format('SELECT %I.add_job(''PARTMAN RE-APPLYING PRIVILEGES TO ALL CHILD TABLES OF: %s'')', v_jobmon_schema, p_parent_table) INTO v_job_id;
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


CREATE FUNCTION @extschema@.create_parent(
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
    , p_date_trunc_interval text DEFAULT NULL)
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
v_higher_parent_epoch           text;
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
    IF p_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        RAISE EXCEPTION 'The pg_partman upsert feature is not supported with native partitioning in PG11+. Use the built-in support for INSERT ON CONFLICT with native partitioning instead.';
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
        RAISE EXCEPTION 'When using native partitioning, you must have created the given parent table as ranged (not list) partitioned already. Ex: CREATE TABLE ... PARTITION BY RANGE ...)';
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


    v_datetime_string := 'YYYY';
    IF p_date_trunc_interval IS NOT NULL THEN

        v_base_timestamp := date_trunc(p_date_trunc_interval, v_start_time);

        IF v_time_interval >= '1 day' THEN
            v_datetime_string := v_datetime_string || '_MM_DD';
        ELSE
            v_datetime_string := v_datetime_string || '_MM_DD_HH24MISS';
        END IF;

    ELSE

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

    END IF; -- end p_date_trunc_interval IF

    RAISE DEBUG 'create_parent(): parent_table: %, v_base_timestamp: %', p_parent_table, v_base_timestamp;

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
        ) SELECT n.nspname, c.relname, p.control, p.epoch
        INTO v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control, v_higher_parent_epoch
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        JOIN top_oid t ON c.oid = t.top_parent_oid
        JOIN @extschema@.part_config p ON p.parent_table = n.nspname||'.'||c.relname;

        IF v_higher_parent_table IS NOT NULL THEN
            SELECT general_type INTO v_higher_control_type
            FROM @extschema@.check_control_type(v_higher_parent_schema, v_higher_parent_table, v_higher_parent_control);
            IF v_higher_control_type <> 'id' or (v_higher_control_type = 'id' AND v_higher_parent_epoch <> 'none') THEN
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


CREATE OR REPLACE FUNCTION @extschema@.show_partition_info(p_child_table text
    , p_partition_interval text DEFAULT NULL
    , p_parent_table text DEFAULT NULL
    , OUT child_start_time timestamptz
    , OUT child_end_time timestamptz
    , OUT child_start_id bigint
    , OUT child_end_id bigint 
    , OUT suffix text)
RETURNS record
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_child_schema          text;
v_child_tablename       text;
v_start_time_string     text; 
v_control               text;
v_control_type          text;
v_datetime_string       text;
v_epoch                 text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_table          text;
v_partition_interval    text;
v_partition_type        text;
v_quarter               text;
v_start_time_epoch      double precision;
v_suffix                text;
v_suffix_position       int;
v_year                  text;

BEGIN
/*
 * Show the data boundries for a given child table as well as the suffix that will be used.
 * Passing the parent table argument improves performance by avoiding a catalog lookup.
 * Passing an interval lets you set one different than the default configured one if desired.
 */

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

SELECT n.nspname, c.relname INTO v_child_schema, v_child_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_child_table, '.', 1)::name
AND c.relname = split_part(p_child_table, '.', 2)::name;

IF v_child_tablename IS NULL THEN
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
    RAISE EXCEPTION 'Child table given does not exist (%)', p_child_table;
END IF;

IF p_parent_table IS NULL THEN
    SELECT n.nspname||'.'|| c.relname INTO v_parent_table
    FROM pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhparent
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhrelid::regclass = p_child_table::regclass;
ELSE
    v_parent_table := p_parent_table;
END IF;

IF p_partition_interval IS NULL THEN
    SELECT control, partition_interval, partition_type, datetime_string, epoch
    INTO v_control, v_partition_interval, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
ELSE
    v_partition_interval := p_partition_interval;
    SELECT control, partition_type, datetime_string, epoch 
    INTO v_control, v_partition_type, v_datetime_string, v_epoch
    FROM @extschema@.part_config WHERE parent_table = v_parent_table;
END IF;

IF v_control IS NULL THEN
    RAISE EXCEPTION 'Parent table of given child not managed by pg_partman: %', v_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_child_schema, v_child_tablename, v_control);

v_suffix_position := (length(v_child_tablename) - position('p_' in reverse(v_child_tablename))) + 2;
v_suffix := substring(v_child_tablename from v_suffix_position);

RAISE DEBUG 'show_partition_info: v_child_schema: %, v_child_tablename: %, v_suffix: %',
            v_child_schema, v_child_tablename, v_suffix;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN

    IF v_partition_type = 'native' THEN
    -- Look at actual partition bounds in catalog and pull values from there. 
    -- For native partitioning, handles any possible interval type much better than old methods in remaining conditions of this IF block 
        SELECT (regexp_match(pg_get_expr(c.relpartbound, c.oid, true)
            , $REGEX$\(([^)]+)\) TO \(([^)]+)\)$REGEX$))[1]::text
        INTO v_start_time_string
        FROM pg_catalog.pg_class c
        JOIN pg_catalog.pg_namespace n
        ON c.relnamespace = n.oid
        WHERE c.relname = v_child_tablename
        AND n.nspname = v_child_schema;

        IF v_control_type = 'time' THEN
            child_start_time := v_start_time_string::timestamptz;
        ELSIF (v_control_type = 'id' AND v_epoch <> 'none') THEN
            -- bigint data type is stored as a single-quoted string in the partition expression. Must strip quotes for valid type-cast.
            v_start_time_string := trim(BOTH '''' FROM v_start_time_string);
            IF v_epoch = 'seconds' THEN
                child_start_time := to_timestamp(v_start_time_string::double precision);
            ELSIF v_epoch = 'milliseconds' THEN
                child_start_time := to_timestamp((v_start_time_string::double precision) / 1000);
            END IF;
        ELSE
            RAISE EXCEPTION 'Unexpected code path in show_partition_info(). Please report this bug with the configuration that lead to it.';
        END IF;

    ELSIF v_partition_interval::interval <> '3 months' OR (v_partition_interval::interval = '3 months' AND v_partition_type = 'time-custom') THEN
       child_start_time := to_timestamp(v_suffix, v_datetime_string);
    ELSE
        -- to_timestamp doesn't recognize 'Q' date string formater. Handle it
        v_year := split_part(v_suffix, 'q', 1);
        v_quarter := split_part(v_suffix, 'q', 2);
        CASE
            WHEN v_quarter = '1' THEN
                child_start_time := to_timestamp(v_year || '-01-01', 'YYYY-MM-DD');
            WHEN v_quarter = '2' THEN
                child_start_time := to_timestamp(v_year || '-04-01', 'YYYY-MM-DD');
            WHEN v_quarter = '3' THEN
                child_start_time := to_timestamp(v_year || '-07-01', 'YYYY-MM-DD');
            WHEN v_quarter = '4' THEN
                child_start_time := to_timestamp(v_year || '-10-01', 'YYYY-MM-DD');
            ELSE
                -- handle case when partition name did not use "q" convetion
                child_start_time := to_timestamp(v_suffix, v_datetime_string);
        END CASE;
    END IF;

    child_end_time := (child_start_time + v_partition_interval::interval);

ELSIF v_control_type = 'id' THEN

    -- Note for future updates, if trigger based partitioning is dropped, do catalog lookup for integer boundaries similar to time.
    child_start_id := v_suffix::bigint;
    child_end_id := (child_start_id + v_partition_interval::bigint) - 1;

ELSE
    RAISE EXCEPTION 'Invalid partition type encountered in show_partition_info()';
END IF;

suffix = v_suffix;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN;

END
$$;


CREATE FUNCTION @extschema@.undo_partition(
    p_parent_table text
    , p_batch_count int DEFAULT 1
    , p_batch_interval text DEFAULT NULL
    , p_keep_table boolean DEFAULT true
    , p_lock_wait numeric DEFAULT 0
    , p_target_table text DEFAULT NULL
    , p_ignored_columns text[] DEFAULT NULL
    , p_drop_cascade boolean DEFAULT false
    , OUT partitions_undone int
    , OUT rows_undone bigint) 
    RETURNS record
    LANGUAGE plpgsql 
    AS $$
DECLARE

ex_context              text;
ex_detail               text;
ex_hint                 text;
ex_message              text;
v_adv_lock              boolean;
v_batch_interval_id     bigint;
v_batch_interval_time   interval;
v_batch_loop_count      int := 0;
v_child_loop_total      bigint := 0;
v_child_table           text;
v_col                   text;
v_column_list           text;
v_control               text;
v_control_type          text;
v_child_min_id          bigint;
v_child_min_time        timestamptz;
v_epoch                 text;
v_function_name         text;
v_jobmon                boolean;
v_jobmon_schema         text;
v_job_id                bigint;
v_inner_loop_count      int;
v_lock_iter             int := 1;
v_lock_obtained         boolean := FALSE;
v_new_search_path       text;
v_old_search_path       text;
v_parent_schema         text;
v_parent_tablename      text;
v_partition_expression  text;
v_partition_interval    text;
v_partition_type        text;
v_relkind               char;
v_row                   record;
v_rowcount              bigint;
v_sql                   text;
v_step_id               bigint;
v_sub_count             int;
v_target_schema         text;
v_target_tablename      text;
v_template_schema       text;
v_template_siblings     int;
v_template_table        text;
v_template_tablename    text;
v_total                 bigint := 0;
v_trig_name             text;
v_undo_count            int := 0;

BEGIN
/*
 * For native, moves data to new, target table since data cannot be moved to parent.
 *      Leaves old parent table as is and does not change name of new table.
 * For trigger-based, moves data to parent
 */

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_native'));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'undo_partition_native already running.';
    partitions_undone = -1;
    RETURN;
END IF;

IF p_parent_table = p_target_table THEN
    RAISE EXCEPTION 'Target table cannot be the same as the parent table';
END IF;

SELECT partition_interval::text
    , partition_type
    , control
    , jobmon
    , epoch
    , template_table
INTO v_partition_interval
    , v_partition_type
    , v_control
    , v_jobmon
    , v_epoch
    , v_template_table
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;

IF v_control IS NULL THEN
    RAISE EXCEPTION 'No configuration found for pg_partman for given parent table: %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_target_table IS NULL THEN
    RAISE EXCEPTION 'Natively partitioned tables require setting the p_target_table option';
END IF;

SELECT n.nspname, c.relname, c.relkind
INTO v_parent_schema, v_parent_tablename, v_relkind 
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;

IF v_parent_tablename IS NULL THEN
    RAISE EXCEPTION 'Given parent table not found in system catalogs: %', p_parent_table;
END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);
IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    IF p_batch_interval IS NULL THEN
        v_batch_interval_time := v_partition_interval::interval;
    ELSE
        v_batch_interval_time := p_batch_interval::interval;
    END IF;
ELSIF v_control_type = 'id' THEN
    IF p_batch_interval IS NULL THEN
        v_batch_interval_id := v_partition_interval::bigint;
    ELSE
        v_batch_interval_id := p_batch_interval::bigint;
    END IF;
ELSE
    RAISE EXCEPTION 'Data type of control column in given partition set must be either data/time or integer.';
END IF;

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Check if any child tables are themselves partitioned or part of an inheritance tree. Prevent undo at this level if so.
-- Need to lock child tables at all levels before multi-level undo can be performed safely.
FOR v_row IN 
    SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(p_parent_table)
LOOP
    SELECT count(*) INTO v_sub_count
    FROM pg_catalog.pg_inherits i
    JOIN pg_catalog.pg_class c ON i.inhparent = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relname = v_row.partition_tablename::name
    AND n.nspname = v_row.partition_schemaname::name;
    IF v_sub_count > 0 THEN
        RAISE EXCEPTION 'Child table for this parent has child table(s) itself (%). Run undo partitioning on this table or remove inheritance first to ensure all data is properly moved to parent', v_row.partition_schemaname||'.'||v_row.partition_tablename;
    END IF;
END LOOP;

IF p_target_table IS NOT NULL THEN
    SELECT n.nspname, c.relname 
    INTO v_target_schema, v_target_tablename 
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(p_target_table, '.', 1)::name
    AND c.relname = split_part(p_target_table, '.', 2)::name;
ELSE
    v_target_schema := v_parent_schema;
    v_target_tablename := v_parent_tablename;
END IF;

IF v_target_tablename IS NULL THEN
    RAISE EXCEPTION 'Given target table not found in system catalogs: %', p_target_table;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    v_job_id := add_job(format('PARTMAN UNDO PARTITIONING: %s', p_parent_table));
    v_step_id := add_step(v_job_id, format('Undoing partitioning for table %s', p_parent_table));
END IF;

v_partition_expression := CASE
    WHEN v_epoch = 'seconds' THEN format('to_timestamp(%I)', v_control)
    WHEN v_epoch = 'milliseconds' THEN format('to_timestamp((%I/1000)::float)', v_control)
    ELSE format('%I', v_control)
END;

-- Stops new time partitions from being made as well as stopping child tables from being dropped if they were configured with a retention period.
UPDATE @extschema@.part_config SET undo_in_progress = true WHERE parent_table = p_parent_table;

IF v_partition_type != 'native' THEN
    -- Stop data going into child tables on non-native partition sets.

    v_trig_name := @extschema@.check_name_length(p_object_name := v_parent_tablename, p_suffix := '_part_trig'); 
    v_function_name := @extschema@.check_name_length(v_parent_tablename, '_part_trig_func', FALSE);

    -- Double-check for proper object existence
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
                partitions_undone = -1;
                RETURN;
            END IF;
        END IF; -- END p_lock_wait IF
        EXECUTE format('DROP TRIGGER IF EXISTS %I ON %I.%I', v_trig_name, v_parent_schema, v_parent_tablename);
    END IF; -- END trigger IF
    v_lock_obtained := FALSE; -- reset for reuse later

    IF v_function_name IS NOT NULL THEN
        EXECUTE format('DROP FUNCTION IF EXISTS %I.%I()', v_parent_schema, v_function_name);
    END IF;

END IF; -- end pg_partman trigger cleanup

IF v_jobmon_schema IS NOT NULL THEN
    IF (v_trig_name IS NOT NULL OR v_function_name IS NOT NULL) THEN
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process. Removed trigger & trigger function');
    ELSE
        PERFORM update_step(v_step_id, 'OK', 'Stopped partition creation process.');
    END IF;
END IF;

-- Generate column list to use in SELECT/INSERT statements below. Allows for exclusion of GENERATED (or any other desired) columns.
v_sql := format ('SELECT ''"''||string_agg(attname, ''","'')||''"'' FROM pg_catalog.pg_attribute a
                    JOIN pg_catalog.pg_class c ON a.attrelid = c.oid
                    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
                    WHERE n.nspname = %L
                    AND c.relname = %L 
                    AND a.attnum > 0 
                    AND a.attisdropped = false'
                  , v_target_schema
                  , v_target_tablename);

IF p_ignored_columns IS NOT NULL THEN
    FOREACH v_col IN ARRAY p_ignored_columns LOOP
        v_sql := v_sql || format(' AND attname != %L ', v_col);
    END LOOP;
END IF;

EXECUTE v_sql INTO v_column_list;

<<outer_child_loop>>
LOOP
    -- Get ordered list of child table in set. Store in variable one at a time per loop until none are left or batch count is reached.
    -- This easily allows it to loop over same child table until empty or move onto next child table after it's dropped
    -- Include the native default table to ensure all data there is removed as well (final parameter = true)
    SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC', TRUE) LIMIT 1;

    EXIT outer_child_loop WHEN v_child_table IS NULL;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Removing child partition: %s.%s', v_parent_schema, v_child_table));
    END IF;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
        EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_time;
    ELSIF v_control_type = 'id' THEN
        EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_id;
    END IF;

    IF v_child_min_time IS NULL AND v_child_min_id IS NULL THEN
        -- No rows left in this child table. Remove from partition set.

        -- lockwait timeout for table drop
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
                partitions_undone = -1;
                RETURN;
            END IF;
        END IF; -- END p_lock_wait IF
        v_lock_obtained := FALSE; -- reset for reuse later

        IF v_partition_type = 'native' THEN
            EXECUTE format('ALTER TABLE %I.%I DETACH PARTITION %I.%I'
                            , v_parent_schema
                            , v_parent_tablename
                            , v_parent_schema
                            , v_child_table);
        ELSE
            EXECUTE format('ALTER TABLE %I.%I NO INHERIT %I.%I'
                            , v_parent_schema
                            , v_child_table
                            , v_parent_schema
                            , v_parent_tablename);
        END IF;

        IF p_keep_table = false THEN
            v_sql := 'DROP TABLE %I.%I';
            IF p_drop_cascade THEN
                v_sql := v_sql || ' CASCADE';
            END IF;
            EXECUTE format(v_sql, v_parent_schema, v_child_table);
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table DROPPED. Moved %s rows to target table', v_child_loop_total));
            END IF;
        ELSE
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Child table DETACHED/UNINHERITED from parent, not DROPPED. Moved %s rows to target table', v_child_loop_total));
            END IF;
        END IF;

        IF v_partition_type = 'time-custom' THEN
            DELETE FROM @extschema@.custom_time_partitions WHERE parent_table = p_parent_table AND child_table = v_parent_schema||'.'||v_child_table;
        END IF;

        v_undo_count := v_undo_count + 1;
        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached
        CONTINUE outer_child_loop; -- skip data moving steps below
    END IF;
    v_inner_loop_count := 1;
    v_child_loop_total := 0;
    <<inner_child_loop>>
    LOOP
        IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
            -- do some locking with timeout, if required
            IF p_lock_wait > 0  THEN
                v_lock_iter := 0;
                WHILE v_lock_iter <= 5 LOOP
                    v_lock_iter := v_lock_iter + 1;
                    BEGIN
                        EXECUTE format('SELECT * FROM %I.%I WHERE %I <= %L FOR UPDATE NOWAIT'
                            , v_parent_schema
                            , v_child_table
                            , v_control
                            , v_child_min_time + (v_batch_interval_time * v_inner_loop_count));
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
                    partitions_undone = -1;
                    RETURN;
                END IF;
            END IF;

            -- Get everything from the current child minimum up to the multiples of the given interval
            EXECUTE format('WITH move_data AS (
                                    DELETE FROM %I.%I WHERE %s <= %L RETURNING %s )
                                  INSERT INTO %I.%I (%5$s) SELECT %5$s FROM move_data'
                , v_parent_schema
                , v_child_table
                , v_partition_expression
                , v_child_min_time + (v_batch_interval_time * v_inner_loop_count)
                , v_column_list
                , v_target_schema
                , v_target_tablename);
            GET DIAGNOSTICS v_rowcount = ROW_COUNT;
            v_total := v_total + v_rowcount;
            v_child_loop_total := v_child_loop_total + v_rowcount;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Moved %s rows to target table.', v_child_loop_total));
            END IF;
            EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
            v_inner_loop_count := v_inner_loop_count + 1;
            v_batch_loop_count := v_batch_loop_count + 1;

            -- Check again if table is empty and go to outer loop again to drop it if so
            EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_time;
            CONTINUE outer_child_loop WHEN v_child_min_time IS NULL;
            
        ELSIF v_control_type = 'id' THEN

            IF p_lock_wait > 0  THEN
                v_lock_iter := 0;
                WHILE v_lock_iter <= 5 LOOP
                    v_lock_iter := v_lock_iter + 1;
                    BEGIN
                        EXECUTE format('SELECT * FROM %I.%I WHERE %I <= %L FOR UPDATE NOWAIT'
                            , v_parent_schema
                            , v_child_table
                            , v_control
                            , v_child_min_id + (v_batch_interval_id * v_inner_loop_count));
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
                   partitions_undone = -1;
                   RETURN;
                END IF;
            END IF;

            -- Get everything from the current child minimum up to the multiples of the given interval
            EXECUTE format('WITH move_data AS (
                                    DELETE FROM %I.%I WHERE %s <= %L RETURNING %s)
                                  INSERT INTO %I.%I (%5$s) SELECT %5$s FROM move_data'
                , v_parent_schema
                , v_child_table
                , v_partition_expression
                , v_child_min_id + (v_batch_interval_id * v_inner_loop_count)
                , v_column_list
                , v_target_schema
                , v_target_tablename);
            GET DIAGNOSTICS v_rowcount = ROW_COUNT;
            v_total := v_total + v_rowcount;
            v_child_loop_total := v_child_loop_total + v_rowcount;
            IF v_jobmon_schema IS NOT NULL THEN
                PERFORM update_step(v_step_id, 'OK', format('Moved %s rows to target table.', v_child_loop_total));
            END IF;
            EXIT inner_child_loop WHEN v_rowcount = 0; -- exit before loop incr if table is empty
            v_inner_loop_count := v_inner_loop_count + 1;
            v_batch_loop_count := v_batch_loop_count + 1;

            -- Check again if table is empty and go to outer loop again to drop it if so
            EXECUTE format('SELECT min(%s) FROM %I.%I', v_partition_expression, v_parent_schema, v_child_table) INTO v_child_min_id;
            CONTINUE outer_child_loop WHEN v_child_min_id IS NULL;

        END IF; -- end v_control_type check

        EXIT outer_child_loop WHEN v_batch_loop_count >= p_batch_count; -- Exit outer FOR loop if p_batch_count is reached

    END LOOP inner_child_loop;
END LOOP outer_child_loop;

SELECT partition_tablename INTO v_child_table FROM @extschema@.show_partitions(p_parent_table, 'ASC', TRUE) LIMIT 1;

IF v_child_table IS NULL THEN
    DELETE FROM @extschema@.part_config WHERE parent_table = p_parent_table;
    
    -- Check if any other config entries still have this template table and don't remove if so
    -- Allows other sibling/parent tables to still keep using in case entire partition set isn't being undone
    SELECT count(*) INTO v_template_siblings FROM @extschema@.part_config WHERE template_table = v_template_table;

    SELECT n.nspname, c.relname
    INTO v_template_schema, v_template_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(v_template_table, '.', 1)::name
    AND c.relname = split_part(v_template_table, '.', 2)::name;

    IF v_template_siblings = 0 AND v_template_tablename IS NOT NULL THEN
        EXECUTE format('DROP TABLE IF EXISTS %I.%I', v_template_schema, v_template_tablename);
    END IF;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Removing config from pg_partman');
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

RAISE NOTICE 'Moved % row(s) to the target table. Removed % partitions.', v_total, v_undo_count;
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Final stats');
    PERFORM update_step(v_step_id, 'OK', format('Moved %s row(s) to the target table. Removed %s partitions.', v_total, v_undo_count));
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
END IF;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

partitions_undone := v_undo_count;
rows_undone := v_total;

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


CREATE OR REPLACE FUNCTION @extschema@.apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT FALSE, p_job_id bigint DEFAULT NULL) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE

ex_context                      text;
ex_detail                       text;
ex_hint                         text;
ex_message                      text;
v_child_exists                  text;
v_child_tablename               text;
v_col                           text;
v_constraint_cols               text[];
v_constraint_col_type           text;
v_constraint_name               text;
v_constraint_valid              boolean;
v_constraint_values             record;
v_control                       text;
v_control_type                  text;
v_datetime_string               text;
v_epoch                         text;
v_existing_constraint_name      text;
v_job_id                        bigint;
v_jobmon                        boolean;
v_jobmon_schema                 text;
v_last_partition                text;
v_last_partition_id             bigint; 
v_last_partition_timestamp      timestamptz;
v_max_id                        bigint;
v_max_timestamp                 timestamptz;
v_new_search_path               text := '@extschema@,pg_temp';
v_old_search_path               text;
v_optimize_constraint           int;
v_parent_schema                 text;
v_parent_table                  text;
v_parent_tablename              text;
v_partition_interval            text;
v_partition_suffix              text;
v_premake                       int;
v_sql                           text;
v_step_id                       bigint;
v_suffix_position               int;
v_type                          text;

BEGIN
/*
 * Apply constraints managed by partman extension
 */

SELECT parent_table
    , partition_type
    , control
    , premake
    , partition_interval
    , optimize_constraint
    , epoch
    , datetime_string
    , constraint_cols
    , jobmon
    , constraint_valid
INTO v_parent_table
    , v_type
    , v_control
    , v_premake
    , v_partition_interval
    , v_optimize_constraint
    , v_epoch
    , v_datetime_string
    , v_constraint_cols
    , v_jobmon
    , v_constraint_valid
FROM @extschema@.part_config
WHERE parent_table = p_parent_table
AND constraint_cols IS NOT NULL;

IF v_constraint_cols IS NULL THEN
    RAISE DEBUG 'apply_constraints: Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    -- Returns silently to allow this function to be simply called by maintenance processes without having to check if config options are set.
    RETURN;
END IF;

SELECT schemaname, tablename 
INTO v_parent_schema, v_parent_tablename 
FROM pg_catalog.pg_tables 
WHERE schemaname = split_part(v_parent_table, '.', 1)::name
AND tablename = split_part(v_parent_table, '.', 2)::name;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

SELECT current_setting('search_path') INTO v_old_search_path;
IF v_jobmon THEN
    SELECT nspname INTO v_jobmon_schema FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_jobmon'::name AND e.extnamespace = n.oid;
    IF v_jobmon_schema IS NOT NULL THEN
        v_new_search_path := '@extschema@,'||v_jobmon_schema||',pg_temp';
    END IF;
END IF;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

IF v_jobmon_schema IS NOT NULL THEN
    IF p_job_id IS NULL THEN
        v_job_id := add_job(format('PARTMAN CREATE CONSTRAINT: %s', v_parent_table));
    ELSE
        v_job_id = p_job_id;
    END IF;
END IF;

-- If p_child_table is null, figure out the partition that is the one right before the optimize_constraint value backwards.
IF p_child_table IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, 'Applying additional constraints: Automatically determining most recent child on which to apply constraints');
    END IF;

    SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(v_parent_table, 'DESC') LIMIT 1;

    IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
        SELECT child_start_time INTO v_last_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, v_parent_table);
        v_partition_suffix := to_char(v_last_partition_timestamp - (v_partition_interval::interval * (v_optimize_constraint + v_premake + 1) ), v_datetime_string);
    ELSIF v_control_type = 'id' THEN
        SELECT child_start_id INTO v_last_partition_id FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, v_parent_table);
        v_partition_suffix := (v_last_partition_id - (v_partition_interval::bigint * (v_optimize_constraint + v_premake + 1) ))::text; 
    END IF;

    RAISE DEBUG 'apply_constraint: v_parent_tablename: %, v_last_partition: %, v_last_partition_timestamp: %, v_partition_suffix: %'
                , v_parent_tablename, v_last_partition, v_last_partition_timestamp, v_partition_suffix;

    v_child_tablename := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', format('Target child table: %s.%s', v_parent_schema, v_child_tablename));
    END IF;
ELSE
    v_child_tablename = split_part(p_child_table, '.', 2);
END IF;
    
IF v_jobmon_schema IS NOT NULL THEN
    v_step_id := add_step(v_job_id, 'Applying additional constraints: Checking if target child table exists');
END IF;

SELECT tablename FROM pg_catalog.pg_tables INTO v_child_exists WHERE schemaname = v_parent_schema::name AND tablename = v_child_tablename::name;
IF v_child_exists IS NULL THEN
    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'NOTICE', format('Target child table (%s) does not exist. Skipping constraint creation.', v_child_tablename));
        IF p_job_id IS NULL THEN
            PERFORM close_job(v_job_id);
        END IF;
    END IF;
    RAISE DEBUG 'Target child table (%) does not exist. Skipping constraint creation.', v_child_tablename;
    EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
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
    WHERE c.relname = v_child_tablename::name
        AND n.nspname = v_parent_schema::name
        AND con.conname LIKE 'partmanconstr_%'
        AND con.contype = 'c' 
        AND a.attname = v_col::name
        AND ARRAY[a.attnum] OPERATOR(pg_catalog.<@) con.conkey
        AND a.attisdropped = false;

    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Applying new constraint on column: %s', v_col));
    END IF;

    IF v_existing_constraint_name IS NOT NULL THEN
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Partman managed constraint already exists on this table (%s) and column (%s). Skipping creation.', v_child_tablename, v_col));
        END IF;
        RAISE DEBUG 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_tablename, v_col ;
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

        IF v_constraint_valid = false THEN
            v_sql := format('%s NOT VALID', v_sql);
        END IF;

        RAISE DEBUG 'Constraint creation query: %', v_sql;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('New constraint created: %s', v_sql));
        END IF;
    ELSE
        RAISE DEBUG 'Given column (%) contains all NULLs. No constraint created', v_col;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Given column (%s) contains all NULLs. No constraint created', v_col));
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Running analyze on partition set: %s', v_parent_table));
    END IF;
    RAISE DEBUG 'Running analyze on partition set: %', v_parent_table;
    EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

    IF v_jobmon_schema IS NOT NULL THEN
        PERFORM update_step(v_step_id, 'OK', 'Done');
    END IF;
END IF;

IF v_jobmon_schema IS NOT NULL THEN
    PERFORM close_job(v_job_id);
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


CREATE FUNCTION @extschema@.create_sub_parent(
    p_top_parent text
    , p_control text
    , p_type text
    , p_interval text
    , p_native_check text DEFAULT NULL
    , p_constraint_cols text[] DEFAULT NULL 
    , p_premake int DEFAULT 4
    , p_start_partition text DEFAULT NULL
    , p_inherit_fk boolean DEFAULT true
    , p_epoch text DEFAULT 'none' 
    , p_upsert text DEFAULT ''
    , p_trigger_return_null boolean DEFAULT true
    , p_jobmon boolean DEFAULT true
    , p_date_trunc_interval text DEFAULT NULL)
RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_interval        interval;
v_child_start_id        bigint;
v_child_start_time      timestamptz;
v_control               text;
v_control_parent_type   text;
v_control_sub_type      text;
v_last_partition        text;
v_new_search_path       text := '@extschema@,pg_temp';
v_old_search_path       text;
v_parent_epoch          text;
v_parent_interval       text;
v_parent_relkind        char;
v_parent_schema         text;
v_parent_tablename      text;
v_parent_type           text;
v_part_col              text;
v_partition_id_array    bigint[];
v_partition_time_array  timestamptz[];
v_relkind               char;
v_recreate_child        boolean := false;
v_row                   record;
v_row_last_part         record;
v_run_maint             boolean;
v_sql                   text;
v_success               boolean := false;
v_template_table        text;
v_top_type              text;

BEGIN
/*
 * Create a partition set that is a subpartition of an already existing partition set.
 * Given the parent table of any current partition set, it will turn all existing children into parent tables of their own partition sets
 *      using the configuration options given as parameters to this function.
 * Uses another config table that allows for turning all future child partitions into a new parent automatically.
 */

SELECT n.nspname, c.relname, c.relkind INTO v_parent_schema, v_parent_tablename, v_parent_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_top_parent, '.', 1)::name
AND c.relname = split_part(p_top_parent, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Please create parent table first: %', p_top_parent;
    END IF;

IF NOT @extschema@.check_partition_type(p_type) THEN
    RAISE EXCEPTION '% is not a valid partitioning type', p_type;
END IF;

IF v_parent_relkind = 'p' AND p_type <> 'native' THEN
    RAISE EXCEPTION 'Cannot create a non-native sub-partition of a native parent table. All levels of a sub-partition set must be either all native or all non-native';
END IF;
 
SELECT partition_type, partition_interval, control, automatic_maintenance, epoch, template_table
INTO v_parent_type, v_parent_interval, v_control, v_run_maint, v_parent_epoch, v_template_table
FROM @extschema@.part_config 
WHERE parent_table = p_top_parent;
IF v_parent_type IS NULL THEN
    RAISE EXCEPTION 'Cannot subpartition a table that is not managed by pg_partman already. Given top parent table not found in @extschema@.part_config: %', p_top_parent;
END IF;

IF p_type = 'native' AND (lower(p_native_check) <> 'yes' OR p_native_check IS NULL) THEN
    RAISE EXCEPTION 'The sub-partitioning of a natively partitioned table is a DESTRUCTIVE process unless all child tables are already natively subpartitioned. All child tables, and therefore ALL DATA, may be destroyed since the parent table must be declared as partitioned on first creation and cannot be altered later. See docs for more info. Set p_native_check parameter to "yes" if you are sure this is ok.';
END IF;

IF p_upsert <> '' THEN
    IF current_setting('server_version_num')::int < 90500 THEN
        RAISE EXCEPTION 'INSERT ... ON CONFLICT (UPSERT) feature is only supported in PostgreSQL 9.5 and later';
    END IF;
    IF p_type = 'native' AND current_setting('server_version_num')::int >= 110000 THEN
        RAISE EXCEPTION 'The pg_partman upsert feature is not supported with native partitioning in PG11+. Use the built-in support for INSERT ON CONFLICT with native partitioning instead.';
    END IF;
END IF;

SELECT general_type INTO v_control_parent_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

SELECT current_setting('search_path') INTO v_old_search_path;
EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_new_search_path, 'false');

-- Add the given parameters to the part_config_sub table first in case create_partition_* functions are called below 
-- All sub-partition parents must use the same template table for native partitioning, so ensure the one from the given parent is obtained and used.
INSERT INTO @extschema@.part_config_sub (
    sub_parent
    , sub_control
    , sub_partition_type
    , sub_partition_interval
    , sub_constraint_cols
    , sub_premake
    , sub_inherit_fk
    , sub_automatic_maintenance
    , sub_epoch
    , sub_upsert
    , sub_jobmon
    , sub_trigger_return_null
    , sub_template_table
    , sub_date_trunc_interval)
VALUES (
    p_top_parent
    , p_control
    , p_type
    , p_interval
    , p_constraint_cols
    , p_premake
    , p_inherit_fk
    , 'on' 
    , p_epoch
    , p_upsert
    , p_jobmon
    , p_trigger_return_null
    , v_template_table
    , p_date_trunc_interval);

FOR v_row IN 
    -- Loop through all current children to turn them into partitioned tables
    SELECT partition_schemaname AS child_schema, partition_tablename AS child_tablename FROM @extschema@.show_partitions(p_top_parent)
LOOP

    SELECT general_type INTO v_control_sub_type FROM @extschema@.check_control_type(v_row.child_schema, v_row.child_tablename, p_control);

    SELECT c.relkind INTO v_relkind
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = v_row.child_schema
    AND c.relname = v_row.child_tablename;

    -- If both parent and sub-parent are the same partition type (time/id), ensure intereval of sub-parent is less than parent
    IF (v_control_parent_type = 'time' AND v_control_sub_type = 'time') OR
       (v_control_parent_type = 'id' AND v_parent_epoch <> 'none' AND v_control_sub_type = 'id' AND p_epoch <> 'none') THEN
        CASE
            WHEN p_interval = 'yearly' THEN
                v_child_interval := '1 year';
            WHEN p_interval = 'quarterly' THEN
                v_child_interval := '3 months';
            WHEN p_interval = 'monthly' THEN
                v_child_interval := '1 month';
            WHEN p_interval  = 'weekly' THEN
                v_child_interval := '1 week';
            WHEN p_interval = 'daily' THEN
                v_child_interval := '1 day';
            WHEN p_interval = 'hourly' THEN
                v_child_interval := '1 hour';
            WHEN p_interval = 'half-hour' THEN
                v_child_interval := '30 mins';
            WHEN p_interval = 'quarter-hour' THEN
                v_child_interval := '15 mins';
            ELSE
                v_child_interval := p_interval::interval;
                IF v_child_interval < '1 second'::interval THEN
                    RAISE EXCEPTION 'Partitioning interval must be 1 second or greater';
                END IF;
        END CASE;

        IF v_child_interval >= v_parent_interval::interval THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than or equal to the given parent interval';
        END IF;
        IF (v_child_interval = '1 week' AND v_parent_interval::interval > '1 week'::interval)
            OR (p_date_trunc_interval = 'week') THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Due to conflicting data boundaries between ISO weeks and any larger interval of time, pg_partman cannot support a sub-partition interval of weekly time periods';
        END IF;

    ELSIF v_control_parent_type = 'id' AND v_control_sub_type = 'id' AND v_parent_epoch = 'none' AND p_epoch = 'none' THEN
        IF p_interval::bigint >= v_parent_interval::bigint THEN
            EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');
            RAISE EXCEPTION 'Sub-partition interval cannot be greater than or equal to the given parent interval';
        END IF;
    END IF;
      
    IF p_type = 'native' THEN
        IF v_relkind <> 'p' THEN 
            -- Not natively partitioned already. Drop it and recreate as such.
            RAISE WARNING 'Child table % is not natively partitioned. Dropping and recreating with native partitioning'
                            , v_row.child_schema||'.'||v_row.child_tablename;
            SELECT child_start_time, child_start_id INTO v_child_start_time, v_child_start_id
            FROM @extschema@.show_partition_info(v_row.child_schema||'.'||v_row.child_tablename
                                                    , v_parent_interval
                                                    , p_top_parent);
            EXECUTE format('DROP TABLE %I.%I', v_row.child_schema, v_row.child_tablename);
            v_recreate_child := true;

            IF v_child_start_id IS NOT NULL THEN
                v_partition_id_array[0] := v_child_start_id;
                PERFORM @extschema@.create_partition_id(p_top_parent, v_partition_id_array, true, p_start_partition);
            ELSIF v_child_start_time IS NOT NULL THEN
                v_partition_time_array[0] := v_child_start_time;
                PERFORM @extschema@.create_partition_time(p_top_parent, v_partition_time_array, true, p_start_partition);
            END IF;
        ELSE
            SELECT a.attname
            INTO v_part_col
            FROM pg_attribute a
            JOIN pg_class c ON a.attrelid = c.oid
            JOIN pg_namespace n ON c.relnamespace = n.oid
            WHERE n.nspname = v_row.child_schema::name
            AND c.relname = v_row.child_tablename::name
            AND attnum IN (SELECT unnest(partattrs) FROM pg_partitioned_table p WHERE a.attrelid = p.partrelid);

            IF p_control <> v_part_col THEN
                RAISE EXCEPTION 'Attempted to natively sub-partition an existing table that has the partition column (%) defined differently than the control column given (%)', v_part_col, p_control;
            ELSE -- Child table is already natively subpartitioned properly. Skip the rest.
                CONTINUE;
            END IF;
        END IF; -- end 'p' relkind check

    END IF; -- end native check

    IF v_recreate_child = false THEN
    -- Always call create_parent() if child table wasn't recreated above.
    -- If it was, the create_partition_*() functions called above also call create_parent if any of the tables
    --  it creates are in the part_config_sub table. Since it was inserted there above,
    --  it should call it appropriately
        v_sql := format('SELECT @extschema@.create_parent(
                 p_parent_table := %L
                , p_control := %L
                , p_type := %L
                , p_interval := %L
                , p_constraint_cols := %L
                , p_premake := %L
                , p_automatic_maintenance := %L
                , p_start_partition := %L
                , p_inherit_fk := %L
                , p_epoch := %L
                , p_upsert := %L
                , p_trigger_return_null := %L
                , p_template_table := %L
                , p_jobmon := %L
                , p_date_trunc_interval := %L)'
            , v_row.child_schema||'.'||v_row.child_tablename
            , p_control
            , p_type
            , p_interval
            , p_constraint_cols
            , p_premake
            , 'on'
            , p_start_partition
            , p_inherit_fk
            , p_epoch
            , p_upsert
            , p_trigger_return_null
            , v_template_table
            , p_jobmon
            , p_date_trunc_interval);
        EXECUTE v_sql;
    END IF; -- end recreate check

END LOOP;

v_success := true;

EXECUTE format('SELECT set_config(%L, %L, %L)', 'search_path', v_old_search_path, 'false');

RETURN v_success;

END
$$;


CREATE OR REPLACE FUNCTION @extschema@.create_partition_time(p_parent_table text, p_partition_times timestamptz[], p_analyze boolean DEFAULT true, p_start_partition text DEFAULT NULL) 
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
RAISE DEBUG 'create_partition_time: v_partition_expression: %', v_partition_expression;

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
        IF v_time < v_sub_timestamp_min OR v_time >= v_sub_timestamp_max THEN

            RAISE DEBUG 'create_partition_time: p_parent_table: %, v_time: %, v_sub_timestamp_min: %, v_sub_timestamp_max: %'
                    , p_parent_table, v_time, v_sub_timestamp_min, v_sub_timestamp_max;

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

    RAISE DEBUG 'create_partition_time v_sql: %', v_sql;
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
                    , EXTRACT('epoch' FROM v_partition_timestamp_start)::bigint
                    , EXTRACT('epoch' FROM v_partition_timestamp_end)::bigint);
            ELSIF v_epoch = 'milliseconds' THEN
                EXECUTE format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I FOR VALUES FROM (%L) TO (%L)'
                    , v_parent_schema
                    , v_parent_tablename
                    , v_parent_schema
                    , v_partition_name
                    , EXTRACT('epoch' FROM v_partition_timestamp_start)::bigint * 1000
                    , EXTRACT('epoch' FROM v_partition_timestamp_end)::bigint * 1000);
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
                            , EXTRACT('epoch' from v_partition_timestamp_start)::bigint
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end)::bigint );
        ELSIF v_epoch = 'milliseconds' THEN
            EXECUTE format('ALTER TABLE %I.%I ADD CONSTRAINT %I CHECK (%I >= %L AND %I < %L)'
                            , v_parent_schema
                            , v_partition_name
                            , v_partition_name||'_partition_int_check'
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_start)::bigint * 1000
                            , v_control
                            , EXTRACT('epoch' from v_partition_timestamp_end)::bigint * 1000);
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
            , sub_date_trunc_interval
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
                , p_jobmon := %L
                , p_start_partition := %L
                , p_date_trunc_interval := %L )'
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
            , v_row.sub_jobmon
            , p_start_partition
            , v_row.sub_date_trunc_interval);
        
        RAISE DEBUG 'create_partition_time (create_parent loop): %', v_sql;
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
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id);

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


CREATE OR REPLACE FUNCTION @extschema@.create_partition_id(p_parent_table text, p_partition_ids bigint[], p_analyze boolean DEFAULT true, p_start_partition text DEFAULT NULL) RETURNS boolean
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
        IF v_id < v_sub_id_min OR v_id >= v_sub_id_max THEN
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

    RAISE DEBUG 'create_partition_id v_sql: %', v_sql;
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
            , sub_date_trunc_interval
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
                , p_jobmon := %L
                , p_start_partition := %L 
                , p_date_trunc_interval := %L )'
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
            , v_row.sub_jobmon
            , p_start_partition
            , v_row.sub_date_trunc_interval);
        RAISE DEBUG 'create_partition_id (create_parent loop): %', v_sql;
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
    PERFORM @extschema@.apply_constraints(p_parent_table, p_job_id := v_job_id);

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
        , sub_subscription_refresh text
        , sub_date_trunc_interval text)
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
        , a.sub_date_trunc_interval
    FROM @extschema@.part_config_sub a
    JOIN child_tables b on a.sub_parent = b.tablename;
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


TRUNCATE partman_preserve_privs_temp;
-- ########################  START POSTGRESQL 11 ONLY SECTION ##############################
DO $pg11only$
DECLARE

v_undo_partition_proc_sql        text;
v_reapply_constraints_proc_sql   text;

BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
-- ########################  START POSTGRESQL 11 ONLY SECTION ##############################

INSERT INTO partman_preserve_privs_temp 
SELECT 'GRANT EXECUTE ON PROCEDURE @extschema@.undo_partition_proc(text, text, int, int, text, boolean, int, int, boolean, text[], boolean) TO '||array_to_string(array_agg('"'||grantee::text||'"'), ',')||';' 
FROM information_schema.routine_privileges
WHERE routine_schema = '@extschema@'
AND routine_name = 'undo_partition_proc'
AND grantee != 'PUBLIC';

DROP PROCEDURE @extschema@.undo_partition_proc(text, text, int, int, text, boolean, int, int, boolean, text[]);

v_undo_partition_proc_sql := $undo_partition_proc$
CREATE PROCEDURE @extschema@.undo_partition_proc(
    p_parent_table text
    , p_interval text DEFAULT NULL
    , p_batch int DEFAULT NULL
    , p_wait int DEFAULT 1
    , p_target_table text DEFAULT NULL
    , p_keep_table boolean DEFAULT true
    , p_lock_wait int DEFAULT 0
    , p_lock_wait_tries int DEFAULT 10
    , p_quiet boolean DEFAULT false
    , p_ignored_columns text[] DEFAULT NULL
    , p_drop_cascade boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock                  boolean;
v_batch_count               int := 0;
v_is_autovac_off            boolean := false;
v_lockwait_count            int := 0;
v_parent_schema             text;
v_parent_tablename          text;
v_partition_type            text;
v_partitions_undone         int;
v_partitions_undone_total   int := 0;
v_row                       record;
v_rows_undone               bigint;
v_target_schema             text;
v_target_tablename          text;
v_sql                       text;
v_total                     bigint := 0;

BEGIN

v_adv_lock := pg_try_advisory_xact_lock(hashtext('pg_partman undo_partition_proc'), hashtext(p_parent_table));
IF v_adv_lock = 'false' THEN
    RAISE NOTICE 'Partman undo_partition_proc already running for given parent table: %.', p_parent_table;
    RETURN;
END IF;

SELECT partition_type
INTO v_partition_type
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF NOT FOUND THEN
    RAISE EXCEPTION 'ERROR: No entry in part_config found for given table: %', p_parent_table;
END IF;

IF v_partition_type = 'native' AND p_target_table IS NULL THEN
    RAISE EXCEPTION 'Natively partitioned table sets require setting the p_target_table parameter to undo partitioning.';
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

IF p_target_table IS NOT NULL THEN
    SELECT n.nspname, c.relname INTO v_target_schema, v_target_tablename
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE n.nspname = split_part(p_target_table, '.', 1)::name
    AND c.relname = split_part(p_target_table, '.', 2)::name;
        IF v_target_tablename IS NULL THEN
            RAISE EXCEPTION 'Unable to find given target table in system catalogs. Ensure it is schema qualified: %', p_target_table;
        END IF;
END IF;

/*
-- Currently no way to catch exception and reset autovac settings back to normal. Until I can do that, leaving this feature out for now
-- Leaving the functions to turn off/reset in to let people do that manually if desired
IF p_autovacuum_on = false THEN         -- Add this parameter back to definition when this is working
    -- Turn off autovac for parent, source table if set, and all child tables
    v_is_autovac_off := @extschema@.autovacuum_off(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT;
END IF;
*/

v_sql := format('SELECT partitions_undone, rows_undone FROM %I.undo_partition (%L, p_keep_table := %L, p_lock_wait := %L'
        , '@extschema@'
        , p_parent_table
        , p_keep_table
        , p_lock_wait);
IF p_interval IS NOT NULL THEN
    v_sql := v_sql || format(', p_batch_interval := %L', p_interval);
END IF;
IF p_target_table IS NOT NULL THEN
    v_sql := v_sql || format(', p_target_table := %L', p_target_table);
END IF;
IF p_ignored_columns IS NOT NULL THEN
    v_sql := v_sql || format(', p_ignored_columns := %L', p_ignored_columns);
END IF;
IF p_drop_cascade IS NOT NULL THEN
    v_sql := v_sql || format(', p_drop_cascade := %L', p_drop_cascade);
END IF;
v_sql := v_sql || ')';
RAISE DEBUG 'partition_data sql: %', v_sql;

LOOP
    EXECUTE v_sql INTO v_partitions_undone, v_rows_undone;
    -- If lock wait timeout, do not increment the counter
    IF v_rows_undone != -1 THEN
        v_batch_count := v_batch_count + 1;
        v_partitions_undone_total := v_partitions_undone_total + v_partitions_undone;
        v_total := v_total + v_rows_undone;
        v_lockwait_count := 0;
    ELSE
        v_lockwait_count := v_lockwait_count + 1;
        IF v_lockwait_count > p_lock_wait_tries THEN
            RAISE EXCEPTION 'Quitting due to inability to get lock on next batch of rows to be moved';
        END IF;
    END IF;
    IF p_quiet = false THEN
        IF v_rows_undone > 0 THEN
            RAISE NOTICE 'Batch: %, Partitions undone this batch: %, Rows undone this batch: %', v_batch_count, v_partitions_undone, v_rows_undone;
        ELSIF v_rows_undone = -1 THEN
            RAISE NOTICE 'Unable to obtain row locks for data to be moved. Trying again...';
        END IF;
    END IF;
    COMMIT;

    -- If no rows left or given batch argument limit is reached
    IF v_rows_undone = 0 OR (p_batch > 0 AND v_batch_count >= p_batch) THEN
        EXIT;
    END IF;

    -- undo_partition functions will remove config entry once last child is dropped
    -- Added here to handle edge-case
    SELECT partition_type
    INTO v_partition_type
    FROM @extschema@.part_config 
    WHERE parent_table = p_parent_table;
    IF NOT FOUND THEN
        EXIT;
    END IF;

    PERFORM pg_sleep(p_wait);
    
    RAISE DEBUG 'v_partitions_undone: %, v_rows_undone: %, v_batch_count: %, v_total: %, v_lockwait_count: %, p_wait: %', v_partitions_undone, p_wait, v_rows_undone, v_batch_count, v_total, v_lockwait_count;
END LOOP;

/*
IF v_is_autovac_off = true THEN
    -- Reset autovac back to default if it was turned off by this procedure
    PERFORM @extschema@.autovacuum_reset(v_parent_schema, v_parent_tablename, v_source_schema, v_source_tablename);
    COMMIT; 
END IF;
*/

IF p_quiet = false THEN
    RAISE NOTICE 'Total partitions undone: %, Total rows moved: %', v_partitions_undone_total, v_total;
END IF;
RAISE NOTICE 'Ensure to VACUUM ANALYZE the old parent & target table after undo has finished';

END
$$;

$undo_partition_proc$;

EXECUTE v_undo_partition_proc_sql;



v_reapply_constraints_proc_sql := $reapply_constraints_proc$
CREATE OR REPLACE PROCEDURE @extschema@.reapply_constraints_proc(p_parent_table text, p_drop_constraints boolean DEFAULT false, p_apply_constraints boolean DEFAULT false, p_wait int DEFAULT 0, p_dryrun boolean DEFAULT false)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock                      boolean;
v_child_stop                    text;
v_control                       text;
v_control_type                  text;
v_datetime_string               text;
v_epoch                         text;
v_last_partition                text;
v_last_partition_id             bigint;
v_last_partition_timestamp      timestamptz;
v_optimize_constraint           int;
v_parent_schema                 text;
v_parent_tablename              text;
v_partition_interval            text;
v_partition_suffix              text;
v_premake                       int;
v_row                           record;
v_sql                           text;

BEGIN
/*
 * Procedure for reapplying additional constraints managed by pg_partman on child tables. See docs for additional info on this special constraint management. 
 * Procedure can run in two distinct modes: 1) Drop all constraints  2) Apply all constraints. 
 * If both modes are run in a single call, drop is run before apply.
 * Typical usage would be to run the drop mode, edit the data, then run apply mode to re-create all constraints on a partition set."
 */

v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman reapply_constraints'));
IF v_adv_lock = false THEN
    RAISE NOTICE 'Partman reapply_constraints_proc already running or another session has not released its advisory lock.';
    RETURN;
END IF;


SELECT control, premake, optimize_constraint, datetime_string, epoch, partition_interval
INTO v_control, v_premake, v_optimize_constraint, v_datetime_string, v_epoch, v_partition_interval
FROM @extschema@.part_config 
WHERE parent_table = p_parent_table;
IF v_premake IS NULL THEN
    RAISE EXCEPTION 'Unable to find given parent in pg_partman config: %. This procedure is only meant to be called on pg_partman managed partition sets.', p_parent_table;
END IF;

SELECT n.nspname, c.relname INTO v_parent_schema, v_parent_tablename
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs. Ensure it is schema qualified: %', p_parent_table;
    END IF;

SELECT general_type INTO v_control_type FROM @extschema@.check_control_type(v_parent_schema, v_parent_tablename, v_control);

-- Determine child table to stop creating constraints on based on optimize_constraint value
-- Same code in apply_constraints.sql
SELECT partition_tablename INTO v_last_partition FROM @extschema@.show_partitions(p_parent_table, 'DESC') LIMIT 1;

IF v_control_type = 'time' OR (v_control_type = 'id' AND v_epoch <> 'none') THEN
    SELECT child_start_time INTO v_last_partition_timestamp FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, p_parent_table);
    v_partition_suffix := to_char(v_last_partition_timestamp - (v_partition_interval::interval * (v_optimize_constraint + v_premake + 1) ), v_datetime_string);
ELSIF v_control_type = 'id' THEN
    SELECT child_start_id INTO v_last_partition_id FROM @extschema@.show_partition_info(v_parent_schema||'.'||v_last_partition, v_partition_interval, p_parent_table);
    v_partition_suffix := (v_last_partition_id - (v_partition_interval::bigint * (v_optimize_constraint + v_premake + 1) ))::text; 
END IF;

v_child_stop := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

v_sql := format('SELECT partition_schemaname, partition_tablename FROM @extschema@.show_partitions(%L, %L)', p_parent_table, 'ASC');

RAISE DEBUG 'reapply_constraint: v_parent_tablename: % , v_partition_suffix: %, v_child_stop: %,  v_sql: %', v_parent_tablename, v_partition_suffix, v_child_stop, v_sql;

v_row := NULL;
FOR v_row IN EXECUTE v_sql LOOP
    IF p_drop_constraints THEN
        IF p_dryrun THEN
            RAISE NOTICE 'DRYRUN NOTICE: Dropping constraints on child table: %.%', v_row.partition_schemaname, v_row.partition_tablename;
        ELSE
            RAISE DEBUG 'reapply_constraint drop: %.%', v_row.partition_schemaname, v_row.partition_tablename;
            PERFORM @extschema@.drop_constraints(p_parent_table, format('%s.%s', v_row.partition_schemaname, v_row.partition_tablename)::text);
        END IF;
    END IF; -- end drop
    COMMIT;

    IF p_apply_constraints THEN
        IF p_dryrun THEN
            RAISE NOTICE 'DRYRUN NOTICE: Applying constraints on child table: %.%', v_row.partition_schemaname, v_row.partition_tablename;
        ELSE
            RAISE DEBUG 'reapply_constraint apply: %.%', v_row.partition_schemaname, v_row.partition_tablename;
            PERFORM @extschema@.apply_constraints(p_parent_table, format('%s.%s', v_row.partition_schemaname, v_row.partition_tablename)::text);
        END IF;
    END IF; -- end apply

    IF v_row.partition_tablename = v_child_stop THEN
        RAISE DEBUG 'reapply_constraint: Reached stop at %.%', v_row.partition_schemaname, v_row.partition_tablename; 
        EXIT; -- stop creating constraints after optimize target is reached
    END IF;
    COMMIT;
    PERFORM pg_sleep(p_wait);
END LOOP;

EXECUTE format('ANALYZE %I.%I', v_parent_schema, v_parent_tablename);

PERFORM pg_advisory_unlock(hashtext('pg_partman reapply_constraints'));
END
$$;
$reapply_constraints_proc$;

EXECUTE v_reapply_constraints_proc_sql;


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

-- ########################  END POSTGRESQL 11 ONLY SECTION ##############################
END IF;
END
$pg11only$;
-- ########################  END POSTGRESQL 11 ONLY SECTION ##############################


DROP TABLE IF EXISTS partman_preserve_privs_temp;
