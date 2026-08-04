-- Preliminary compatability with PostgreSQL 12.

-- IMPORTANT SUPPORT CHANGE: As of PostgreSQL 12's stable release, version 9.4 of PostgreSQL will no longer officially be supported by pg_partman. It will likely continue to function without any issue, but any bugs tied specifically to that version and lower will no longer be fixed and new features that are not compatible with 9.4 may be introduced.

-- IMPORTANT CHANGE: The UNLOGGED status of a partition set in native partitioning has changed to be managed via the template table, no longer the parent table. 
    -- Currently setting a natively partitioned parent table as UNLOGGED/LOGGED is not a property that can be changed with an ALTER TABLE. So if the state is later changed to LOGGED (or vice versa), this property will not change for future child tables. Current child tables were never handled via this manner before and always had to be fixed manually. 
    -- If any of your native partition sets are UNLOGGED, be sure to set this property on the template table before upgrading to this version. Otherwise new child tables may be created as LOGGED.
    -- pg_partman will maintain this behavior until core PostgreSQL determines a definative way that UNLOGGED changes will be handled.
    -- See reported bug at https://www.postgresql.org/message-id/flat/15954-b61523bed4b110c4%40postgresql.org

-- As of PostgreSQL 12, pg_partman will no longer support inheriting OIDs since the special column behavior has been removed. See the release notes for PG12 for more information on this change. The feature has not been deprecated from pg_partman for older versions of PostgreSQL, but all tests involving the feature have been removed and any bugs involving it will likely not be fixed unless it is a compatability issue with 12+. It is recommended to remove any dependencies on special OID behavior that you previously had in your partition sets at this time.
    -- Fixed bug that prevented pg_partman from creating child tables in PG12+ due to useage of pg_class.relhasoids column. (Github Issue #256)

-- A new configuration option, "constraint_valid", has been added to the part_config(_sub) table to control whether the additonal constraints that pg_partman can manage (see "Constraint Exclusion" section in pg_partman.md) are added in a NOT VALID state. By default the constraints are added as VALID as they were before. Note that if this config is changed to "false", constraint exclusion will not work until the constraints are validated.

-- Fixed bug where primary key/unique indexes applied to pre-existing template tables were not being applied to the DEFAULT partition created in PG11+ native partitioning. Thanks to @dtseiler on Github for testing and reporting this. (Github Issue #266)

-- Fixed bug that was causing error in the retention management of time-based partitioning sets. If retention would attempt to remove the last child table, maintenance would throw an error about a missing table instead of just a warning about the last table attempting to be dropped. Thanks to @LaVoCG on Github for the examples to reproduce this scenario. (Github Issues #227 & #237)

-- Changed default option to false on PG11+ for whether the maintenance background worker will run an ANALYZE whenever a child table is created. If this is desired, please set the option pg_partman_bgw.analyze to true in your postgresql.conf. (Github Issue #262)

-- Properly throw an error with create_parent() when the data type of the partition column is numeric. Previously would not report any errors but would also not add the proper data to the part_config table so future maintenance would not work. Currently testing to see if numeric partitioning can be properly supported in pg_partman. (Github Issue #265)


ALTER TABLE @extschema@.part_config ADD COLUMN constraint_valid boolean DEFAULT true NOT NULL;
ALTER TABLE @extschema@.part_config_sub ADD COLUMN sub_constraint_valid boolean DEFAULT true NOT NULL;

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
        , sub_constraint_valid)
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
        , v_row.sub_constraint_valid);

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

    -- Same INCLUDING list is used in create_partition_*()
    v_sql := v_sql || format(' TABLE %I.%I (LIKE %I.%I INCLUDING DEFAULTS INCLUDING CONSTRAINTS INCLUDING STORAGE INCLUDING COMMENTS)'
        , v_parent_schema, v_default_partition, v_parent_schema, v_parent_tablename);
    EXECUTE v_sql;
    v_sql := format('ALTER TABLE %I.%I ATTACH PARTITION %I.%I DEFAULT'
        , v_parent_schema, v_parent_tablename, v_parent_schema, v_default_partition);
    EXECUTE v_sql;

    -- Ensure any primary/unique keys on premade template tables are applied 
    PERFORM @extschema@.inherit_template_properties(p_parent_table, v_parent_schema, v_default_partition);

    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_default_partition, v_parent_tablespace);
    END IF;

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

    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- NOTE: Need to handle this differently when index inheritance is supported natively
        -- Cannot include indexes since they cannot exist on native parents.
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

    ELSE
        -- Handled in inherit_template_properties for native because CREATE TABLE ignores TABLESPACE flag for native partition parents
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
            , sub_automatic_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_template_table
            , sub_inherit_privileges
            , sub_constraint_valid
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
            , inherit_privileges = v_row.sub_inherit_privileges
            , constraint_valid = v_row.sub_constraint_valid
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

    SELECT sub_partition_type, sub_control INTO v_sub_partition_type, v_sub_control 
    FROM @extschema@.part_config_sub 
    WHERE sub_parent = p_parent_table;
    IF v_sub_partition_type = 'native' THEN
        -- NOTE: Need to handle this differently when index inheritance is supported natively
        -- Cannot include indexes since they cannot exist on native parents
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

    IF v_parent_tablespace IS NOT NULL THEN
        EXECUTE format('ALTER TABLE %I.%I SET TABLESPACE %I', v_parent_schema, v_partition_name, v_parent_tablespace);
    END IF;

    IF v_partition_type = 'native' THEN

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
    ELSE
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
            , sub_automatic_maintenance
            , sub_infinite_time_partitions
            , sub_jobmon
            , sub_trigger_exception_handling
            , sub_template_table
            , sub_inherit_privileges
            , sub_constraint_valid
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
            , inherit_privileges = v_row.sub_inherit_privileges
            , constraint_valid = v_row.sub_constraint_valid
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


CREATE OR REPLACE FUNCTION @extschema@.drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
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
v_step_id                   bigint;

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
    IF v_retention < (CURRENT_TIMESTAMP - (v_partition_timestamp + v_partition_interval)) THEN

        -- Do not allow final partition to be dropped
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 THEN
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
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
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
v_step_id                   bigint;

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
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention
        , v_retention_keep_table
        , v_retention_keep_index
        , v_retention_schema
        , v_jobmon
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
    INTO
        v_partition_interval
        , v_partition_type
        , v_control
        , v_retention_keep_table
        , v_retention_keep_index
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

        -- Do not allow final partition to be dropped
        SELECT count(*) INTO v_count FROM @extschema@.show_partitions(p_parent_table);
        IF v_count = 1 THEN
            RAISE WARNING 'Attempt to drop final partition in partition set % as part of retention policy. Advise reviewing retention policy and/or data entry into the partition set.', p_parent_table;
            CONTINUE;
        END IF;

        -- Only create a jobmon entry if there's actual retention work done
        IF v_jobmon_schema IS NOT NULL AND v_job_id IS NULL THEN
            v_job_id := add_job(format('PARTMAN DROP ID PARTITION: %s', p_parent_table));
        END IF;

        IF v_jobmon_schema IS NOT NULL THEN
            v_step_id := add_step(v_job_id, format('Detach/Uninherit table %s.%s from %s', v_row.partition_schemaname, v_row.partition_tablename, p_parent_table));
        END IF;
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
        IF v_retention_schema IS NULL THEN
            IF v_retention_keep_table = false THEN
                IF v_jobmon_schema IS NOT NULL THEN
                    v_step_id := add_step(v_job_id, format('Drop table %s.%s', v_row.partition_schemaname, v_row.partition_tablename));
                END IF;
                EXECUTE format('DROP TABLE %I.%I CASCADE', v_row.partition_schemaname, v_row.partition_tablename);
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



CREATE OR REPLACE FUNCTION @extschema@.apply_constraints(p_parent_table text, p_child_table text DEFAULT NULL, p_analyze boolean DEFAULT FALSE, p_job_id bigint DEFAULT NULL, p_debug boolean DEFAULT FALSE) RETURNS void
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
    IF p_debug THEN
        RAISE NOTICE 'Given parent table (%) not set up for constraint management (constraint_cols is NULL)', p_parent_table;
    END IF;
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
        v_partition_suffix := (v_last_partition_id - (v_partition_interval::int * (v_optimize_constraint + v_premake + 1) ))::text; 
    END IF;

    v_child_tablename := @extschema@.check_name_length(v_parent_tablename, v_partition_suffix, TRUE);

    IF p_debug THEN
        RAISE NOTICE 'apply_constraint: v_parent_tablename: % , v_partition_suffix: %', v_parent_tablename, v_partition_suffix;
    END IF;

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
    IF p_debug THEN
        RAISE NOTICE 'Target child table (%) does not exist. Skipping constraint creation.', v_child_tablename;
    END IF;
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
        IF p_debug THEN
            RAISE NOTICE 'Partman managed constraint already exists on this table (%) and column (%). Skipping creation.', v_child_tablename, v_col ;
        END IF;
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

        IF p_debug THEN
            RAISE NOTICE 'Constraint creation query: %', v_sql;
        END IF;
        EXECUTE v_sql;

        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'OK', format('New constraint created: %s', v_sql));
        END IF;
    ELSE
        IF p_debug THEN
            RAISE NOTICE 'Given column (%) contains all NULLs. No constraint created', v_col;
        END IF;
        IF v_jobmon_schema IS NOT NULL THEN
            PERFORM update_step(v_step_id, 'NOTICE', format('Given column (%s) contains all NULLs. No constraint created', v_col));
        END IF;
    END IF;

END LOOP;

IF p_analyze THEN
    IF v_jobmon_schema IS NOT NULL THEN
        v_step_id := add_step(v_job_id, format('Applying additional constraints: Running analyze on partition set: %s', v_parent_table));
    END IF;
    IF p_debug THEN
        RAISE NOTICE 'Running analyze on partition set: %', v_parent_table;
    END IF;

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


CREATE OR REPLACE FUNCTION @extschema@.inherit_template_properties (p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_relkind         char;
v_child_schema          text;
v_child_tablename       text;
v_child_unlogged        char;
v_dupe_found            boolean := false;
v_fk_list               record;
v_index_list            record;
v_inherit_fk            boolean;
v_parent_index_list     record;
v_parent_oid            oid;
v_parent_table          text;
v_sql                   text;
v_template_oid          oid;
v_template_schemaname   text;
v_template_table        text;
v_template_tablename    name;
v_template_tablespace   name;
v_template_unlogged     char;

BEGIN
/*
 * Function to inherit the properties of the template table to newly created child tables.
 * Currently used for PostgreSQL 10 to inherit indexes and FKs since that is not natively available
 * For PG11, used to inherit non-partition-key unique indexes & primary keys
 */

SELECT parent_table, template_table, inherit_fk
INTO v_parent_table, v_template_table, v_inherit_fk
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;
IF v_parent_table IS NULL THEN
    RAISE EXCEPTION 'Given parent table has no configuration in pg_partman: %', p_parent_table;
ELSIF v_template_table IS NULL THEN
    RAISE EXCEPTION 'No template table set in configuration for given parent table: %', p_parent_table;
END IF;

SELECT c.oid INTO v_parent_oid
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs: %', p_parent_table;
    END IF;
 
SELECT n.nspname, c.relname, c.relkind INTO v_child_schema, v_child_tablename, v_child_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_child_schema::name
AND c.relname = p_child_tablename::name;
    IF v_child_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given child table in system catalogs: %.%', v_child_schema, v_child_tablename;
    END IF;
       
IF v_child_relkind = 'p' THEN
    -- Subpartitioned parent, do not apply properties
    RAISE DEBUG 'inherit_template_properties: found given child is subpartition parent, so properties not inherited';
    RETURN false;
END IF;

v_template_schemaname := split_part(v_template_table, '.', 1)::name;
v_template_tablename :=  split_part(v_template_table, '.', 2)::name;

 SELECT c.oid, ts.spcname INTO v_template_oid, v_template_tablespace
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
WHERE n.nspname = v_template_schemaname
AND c.relname = v_template_tablename;
    IF v_template_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find configured template table in system catalogs: %', v_template_table;
    END IF;

-- Index creation (Required for all indexes in PG10. Only for non-unique, non-partition key indexes in PG11)
IF current_setting('server_version_num')::int >= 100000 THEN
    FOR v_index_list IN 
        SELECT
        array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
        , i.indisprimary
        , i.indisunique
        , ( SELECT array_agg( a.attname ORDER by x.r )
            FROM pg_catalog.pg_attribute a
            JOIN ( SELECT k, row_number() over () as r
                    FROM unnest(i.indkey) k ) as x
            ON a.attnum = x.k AND a.attrelid = i.indrelid
        ) AS indkey_names
        , c.relname AS index_name
        , ts.spcname AS tablespace_name
        FROM pg_catalog.pg_index i
        JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
        LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
        WHERE i.indrelid = v_template_oid
        AND i.indisvalid
        ORDER BY 1
    LOOP
        IF current_setting('server_version_num')::int >= 110000 THEN
            FOR v_parent_index_list IN 
                SELECT
                array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
                , i.indisprimary
                , ( SELECT array_agg( a.attname ORDER by x.r )
                    FROM pg_catalog.pg_attribute a
                    JOIN ( SELECT k, row_number() over () as r
                            FROM unnest(i.indkey) k ) as x
                    ON a.attnum = x.k AND a.attrelid = i.indrelid
                ) AS indkey_names
                FROM pg_catalog.pg_index i
                WHERE i.indrelid = v_parent_oid
                AND i.indisvalid
                ORDER BY 1
            LOOP

                IF v_parent_index_list.indisprimary AND v_index_list.indisprimary THEN
                    IF v_parent_index_list.indkey_names = v_index_list.indkey_names THEN
                        RAISE DEBUG 'Ignoring duplicate primary key on template table: % ', v_index_list.indkey_names;
                        v_dupe_found := true;
                        CONTINUE; -- only continue within this nested loop
                    END IF;
                END IF;

                IF v_parent_index_list.statement = v_index_list.statement THEN
                    RAISE DEBUG 'Ignoring duplicate index on template table: %', v_index_list.statement;
                    v_dupe_found := true;
                    CONTINUE; -- only continue within this nested loop
                END IF;

            END LOOP; -- end parent index loop
        END IF; -- End PG11 check

        IF v_dupe_found = true THEN
            -- Only used in PG11 and should skip trying to create indexes that already existed on the parent
            CONTINUE;
        END IF;

        IF v_index_list.indisprimary THEN
            v_sql := format('ALTER TABLE %I.%I ADD PRIMARY KEY (%s)'
                            , v_child_schema
                            , v_child_tablename
                            , '"' || array_to_string(v_index_list.indkey_names, '","') || '"');
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' USING INDEX TABLESPACE %I', v_index_list.tablespace_name);
            END IF;
            RAISE DEBUG 'Create pk: %', v_sql;
            EXECUTE v_sql;
        ELSE
            -- statement column should be just the portion of the index definition that defines what it actually is
            v_sql := format('CREATE %s INDEX ON %I.%I %s', CASE WHEN v_index_list.indisunique = TRUE THEN 'UNIQUE' ELSE '' END, v_child_schema, v_child_tablename, v_index_list.statement);
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' TABLESPACE %I', v_index_list.tablespace_name);
            END IF;

            RAISE DEBUG 'Create index: %', v_sql;
            EXECUTE v_sql;

        END IF;

    END LOOP;
END IF; 
-- End index creation

-- Foreign key creation (PG10 only)
IF current_setting('server_version_num')::int >= 100000 AND current_setting('server_version_num')::int < 110000 THEN
    IF v_inherit_fk THEN
        FOR v_fk_list IN 
            SELECT pg_get_constraintdef(con.oid) AS constraint_def
            FROM pg_catalog.pg_constraint con
            JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
            WHERE c.oid = v_template_oid
            AND contype = 'f'
        LOOP
            v_sql := format('ALTER TABLE %I.%I ADD %s', v_child_schema, v_child_tablename, v_fk_list.constraint_def);
            RAISE DEBUG 'Create FK: %', v_sql;
            EXECUTE v_sql;
        END LOOP;
    END IF;
END IF;
-- End foreign key creation

-- Tablespace inheritance
IF v_template_tablespace IS NOT NULL THEN
    v_sql := format('ALTER TABLE %I.%I SET TABLESPACE %I', v_child_schema, v_child_tablename, v_template_tablespace);
    RAISE DEBUG 'Alter tablespace: %', v_sql;
    EXECUTE v_sql;
END IF;

-- UNLOGGED status. Currently waiting on final stance of how native will handle this property being changed for its children. 
-- See release notes for v4.2.0
SELECT relpersistence INTO v_template_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = v_template_schemaname
AND c.relname = v_template_tablename;

SELECT relpersistence INTO v_child_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = v_child_schema::name
AND c.relname = v_child_tablename::name;

IF v_template_unlogged = 'u' AND v_child_unlogged = 'p'  THEN
    v_sql := format ('ALTER TABLE %I.%I SET UNLOGGED', v_child_schema, v_child_tablename);
    RAISE DEBUG 'Alter UNLOGGED: %', v_sql;
    EXECUTE v_sql;     
ELSIF v_template_unlogged = 'p' AND v_child_unlogged = 'u'  THEN
    v_sql := format ('ALTER TABLE %I.%I SET LOGGED', v_child_schema, v_child_tablename);
    RAISE DEBUG 'Alter UNLOGGED: %', v_sql;
    EXECUTE v_sql;     
END IF;

RETURN true;

END
$$;

